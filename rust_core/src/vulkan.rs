//! Vulkan GPU scene renderer for the mission editor.
//!
//! Rust owns the Vulkan instance, device, Win32 surface, swapchain,
//! synchronization objects, and GPU compute compositor (`vulkan_scene.rs`).

use ash::{vk, Entry};
use std::cell::RefCell;
use std::ffi::{c_char, c_void, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::slice;

#[path = "vulkan_scene.rs"]
mod scene;

const RS_OK: i32 = 0;
const RS_VULKAN_MINIMIZED: i32 = 1;
const RS_VULKAN_SWAPCHAIN_OUT_OF_DATE: i32 = 2;
const RS_ERR_BAD_ARG: i32 = -1;
const RS_ERR_PANIC: i32 = -3;
const RS_ERR_VULKAN_UNAVAILABLE: i32 = -10;
const RS_ERR_VULKAN_RUNTIME: i32 = -11;
const MAX_FRAMES_IN_FLIGHT: usize = 2;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum VulkanStatus {
    Ok,
    Minimized,
    SwapchainOutOfDate,
}

thread_local! {
    static LAST_ERROR: RefCell<String> = const { RefCell::new(String::new()) };
}

fn set_last_error(message: impl Into<String>) {
    LAST_ERROR.with(|slot| *slot.borrow_mut() = message.into());
}

#[link(name = "kernel32")]
unsafe extern "system" {
    fn GetModuleHandleW(module_name: *const u16) -> *mut c_void;
}

struct StagingBuffer {
    buffer: vk::Buffer,
    memory: vk::DeviceMemory,
    mapped: *mut u8,
    len: usize,
}

impl Default for StagingBuffer {
    fn default() -> Self {
        Self {
            buffer: vk::Buffer::null(),
            memory: vk::DeviceMemory::null(),
            mapped: ptr::null_mut(),
            len: 0,
        }
    }
}

struct FrameResources {
    command_buffer: vk::CommandBuffer,
    image_available: vk::Semaphore,
    fence: vk::Fence,
}

impl FrameResources {
    fn new(command_buffer: vk::CommandBuffer) -> Self {
        Self {
            command_buffer,
            image_available: vk::Semaphore::null(),
            fence: vk::Fence::null(),
        }
    }
}

struct VulkanRenderer {
    _entry: Entry,
    instance: ash::Instance,
    surface_loader: ash::khr::surface::Instance,
    surface: vk::SurfaceKHR,
    physical_device: vk::PhysicalDevice,
    device: ash::Device,
    queue_family: u32,
    queue: vk::Queue,
    swapchain_loader: ash::khr::swapchain::Device,
    swapchain: vk::SwapchainKHR,
    swapchain_images: Vec<vk::Image>,
    present_semaphores: Vec<vk::Semaphore>,
    present_mode: vk::PresentModeKHR,
    scene: Option<scene::SceneGpu>,
    image_initialized: Vec<bool>,
    swapchain_format: vk::Format,
    swapchain_extent: vk::Extent2D,
    requested_extent: vk::Extent2D,
    vsync: bool,
    swapchain_dirty: bool,
    command_pool: vk::CommandPool,
    frames: Vec<FrameResources>,
    current_frame: usize,
}

impl VulkanRenderer {
    unsafe fn new(hwnd: *mut c_void) -> Result<Self, String> {
        if hwnd.is_null() {
            return Err("Vulkan received a null Win32 window handle".into());
        }

        let entry = unsafe { Entry::load() }
            .map_err(|error| format!("could not load vulkan-1.dll: {error}"))?;
        let application_name = CString::new("FinalSun FinalAlert Vulkan Renderer").unwrap();
        let engine_name = CString::new("mission_editor_rust_core").unwrap();
        let application_info = vk::ApplicationInfo::default()
            .application_name(&application_name)
            .application_version(vk::make_api_version(0, 2, 0, 0))
            .engine_name(&engine_name)
            .engine_version(vk::make_api_version(0, 1, 0, 0))
            .api_version(vk::API_VERSION_1_0);
        let instance_extensions = [
            ash::khr::surface::NAME.as_ptr(),
            ash::khr::win32_surface::NAME.as_ptr(),
        ];
        let instance_info = vk::InstanceCreateInfo::default()
            .application_info(&application_info)
            .enabled_extension_names(&instance_extensions);
        let instance = unsafe { entry.create_instance(&instance_info, None) }
            .map_err(|error| format!("vkCreateInstance failed: {error:?}"))?;

        let surface_loader = ash::khr::surface::Instance::new(&entry, &instance);
        let win32_surface_loader = ash::khr::win32_surface::Instance::new(&entry, &instance);
        let hinstance = unsafe { GetModuleHandleW(ptr::null()) };
        if hinstance.is_null() {
            unsafe { instance.destroy_instance(None) };
            return Err("GetModuleHandleW failed while creating the Vulkan surface".into());
        }
        let surface_info = vk::Win32SurfaceCreateInfoKHR::default()
            .hinstance(hinstance as isize)
            .hwnd(hwnd as isize);
        let surface =
            match unsafe { win32_surface_loader.create_win32_surface(&surface_info, None) } {
                Ok(surface) => surface,
                Err(error) => {
                    unsafe { instance.destroy_instance(None) };
                    return Err(format!("vkCreateWin32SurfaceKHR failed: {error:?}"));
                }
            };

        let (physical_device, queue_family) =
            match unsafe { Self::select_physical_device(&instance, &surface_loader, surface) } {
                Ok(selected) => selected,
                Err(error) => {
                    unsafe {
                        surface_loader.destroy_surface(surface, None);
                        instance.destroy_instance(None);
                    }
                    return Err(error);
                }
            };

        let queue_priorities = [1.0_f32];
        let queue_info = [vk::DeviceQueueCreateInfo::default()
            .queue_family_index(queue_family)
            .queue_priorities(&queue_priorities)];
        let device_extensions = [ash::khr::swapchain::NAME.as_ptr()];
        let device_info = vk::DeviceCreateInfo::default()
            .queue_create_infos(&queue_info)
            .enabled_extension_names(&device_extensions);
        let device = match unsafe { instance.create_device(physical_device, &device_info, None) } {
            Ok(device) => device,
            Err(error) => {
                unsafe {
                    surface_loader.destroy_surface(surface, None);
                    instance.destroy_instance(None);
                }
                return Err(format!("vkCreateDevice failed: {error:?}"));
            }
        };
        let queue = unsafe { device.get_device_queue(queue_family, 0) };
        let swapchain_loader = ash::khr::swapchain::Device::new(&instance, &device);

        let mut renderer = Self {
            _entry: entry,
            instance,
            surface_loader,
            surface,
            physical_device,
            device,
            queue_family,
            queue,
            swapchain_loader,
            swapchain: vk::SwapchainKHR::null(),
            swapchain_images: Vec::new(),
            present_semaphores: Vec::new(),
            present_mode: vk::PresentModeKHR::FIFO,
            scene: None,
            image_initialized: Vec::new(),
            swapchain_format: vk::Format::UNDEFINED,
            swapchain_extent: vk::Extent2D::default(),
            requested_extent: vk::Extent2D::default(),
            vsync: true,
            swapchain_dirty: true,
            command_pool: vk::CommandPool::null(),
            frames: Vec::new(),
            current_frame: 0,
        };
        renderer.create_command_resources()?;
        renderer.create_sync_resources()?;
        Ok(renderer)
    }

    unsafe fn select_physical_device(
        instance: &ash::Instance,
        surface_loader: &ash::khr::surface::Instance,
        surface: vk::SurfaceKHR,
    ) -> Result<(vk::PhysicalDevice, u32), String> {
        let devices = unsafe { instance.enumerate_physical_devices() }
            .map_err(|error| format!("vkEnumeratePhysicalDevices failed: {error:?}"))?;
        let mut candidates = Vec::new();
        for physical_device in devices {
            let extensions =
                unsafe { instance.enumerate_device_extension_properties(physical_device) }
                    .map_err(|error| format!("device extension query failed: {error:?}"))?;
            let has_swapchain = extensions.iter().any(|extension| {
                let name = unsafe { CStr::from_ptr(extension.extension_name.as_ptr()) };
                name == ash::khr::swapchain::NAME
            });
            if !has_swapchain {
                continue;
            }
            let queues =
                unsafe { instance.get_physical_device_queue_family_properties(physical_device) };
            for (index, properties) in queues.iter().enumerate() {
                if !properties
                    .queue_flags
                    .contains(vk::QueueFlags::GRAPHICS | vk::QueueFlags::COMPUTE)
                {
                    continue;
                }
                let can_present = unsafe {
                    surface_loader.get_physical_device_surface_support(
                        physical_device,
                        index as u32,
                        surface,
                    )
                }
                .map_err(|error| format!("surface-support query failed: {error:?}"))?;
                if can_present {
                    let properties =
                        unsafe { instance.get_physical_device_properties(physical_device) };
                    let score = match properties.device_type {
                        vk::PhysicalDeviceType::DISCRETE_GPU => 3,
                        vk::PhysicalDeviceType::INTEGRATED_GPU => 2,
                        _ => 1,
                    };
                    candidates.push((score, physical_device, index as u32));
                    break;
                }
            }
        }
        candidates
            .into_iter()
            .max_by_key(|candidate| candidate.0)
            .map(|(_, device, queue)| (device, queue))
            .ok_or_else(|| {
                "no Vulkan device supports graphics, compute, presentation and VK_KHR_swapchain"
                    .into()
            })
    }

    unsafe fn create_command_resources(&mut self) -> Result<(), String> {
        let pool_info = vk::CommandPoolCreateInfo::default()
            .queue_family_index(self.queue_family)
            .flags(vk::CommandPoolCreateFlags::RESET_COMMAND_BUFFER);
        self.command_pool = unsafe { self.device.create_command_pool(&pool_info, None) }
            .map_err(|error| format!("vkCreateCommandPool failed: {error:?}"))?;
        let allocate_info = vk::CommandBufferAllocateInfo::default()
            .command_pool(self.command_pool)
            .level(vk::CommandBufferLevel::PRIMARY)
            .command_buffer_count(MAX_FRAMES_IN_FLIGHT as u32);
        self.frames = unsafe { self.device.allocate_command_buffers(&allocate_info) }
            .map_err(|error| format!("vkAllocateCommandBuffers failed: {error:?}"))?
            .into_iter()
            .map(FrameResources::new)
            .collect();
        Ok(())
    }

    unsafe fn create_sync_resources(&mut self) -> Result<(), String> {
        let semaphore_info = vk::SemaphoreCreateInfo::default();
        let fence_info = vk::FenceCreateInfo::default().flags(vk::FenceCreateFlags::SIGNALED);
        for frame in &mut self.frames {
            frame.image_available = unsafe { self.device.create_semaphore(&semaphore_info, None) }
                .map_err(|error| format!("vkCreateSemaphore failed: {error:?}"))?;
            frame.fence = unsafe { self.device.create_fence(&fence_info, None) }
                .map_err(|error| format!("vkCreateFence failed: {error:?}"))?;
        }
        Ok(())
    }

    unsafe fn choose_surface_format(&self) -> Result<vk::SurfaceFormatKHR, String> {
        let formats = unsafe {
            self.surface_loader
                .get_physical_device_surface_formats(self.physical_device, self.surface)
        }
        .map_err(|error| format!("surface-format query failed: {error:?}"))?;
        if formats.is_empty() {
            return Err("the Vulkan surface reported no pixel formats".into());
        }
        if formats.len() == 1 && formats[0].format == vk::Format::UNDEFINED {
            return Ok(vk::SurfaceFormatKHR {
                format: vk::Format::B8G8R8A8_UNORM,
                color_space: formats[0].color_space,
            });
        }
        let preferred = [
            vk::Format::B8G8R8A8_UNORM,
            vk::Format::B8G8R8A8_SRGB,
            vk::Format::R8G8B8A8_UNORM,
            vk::Format::R8G8B8A8_SRGB,
        ];
        for wanted in preferred {
            if let Some(format) = formats.iter().find(|format| format.format == wanted) {
                return Ok(*format);
            }
        }
        Err("the Vulkan surface has no supported 32-bit BGRA/RGBA format".into())
    }

    unsafe fn create_staging_buffer(&self, len: usize) -> Result<StagingBuffer, String> {
        let size = u64::try_from(len).map_err(|_| "staging buffer size overflow")?;
        let buffer_info = vk::BufferCreateInfo::default()
            .size(size)
            .usage(
                vk::BufferUsageFlags::TRANSFER_SRC
                    | vk::BufferUsageFlags::TRANSFER_DST
                    | vk::BufferUsageFlags::STORAGE_BUFFER,
            )
            .sharing_mode(vk::SharingMode::EXCLUSIVE);
        let buffer = unsafe { self.device.create_buffer(&buffer_info, None) }
            .map_err(|error| format!("vkCreateBuffer failed: {error:?}"))?;
        let requirements = unsafe { self.device.get_buffer_memory_requirements(buffer) };
        let memory_properties = unsafe {
            self.instance
                .get_physical_device_memory_properties(self.physical_device)
        };
        let required_flags =
            vk::MemoryPropertyFlags::HOST_VISIBLE | vk::MemoryPropertyFlags::HOST_COHERENT;
        let memory_type = (0..memory_properties.memory_type_count).find(|index| {
            requirements.memory_type_bits & (1_u32 << index) != 0
                && memory_properties.memory_types[*index as usize]
                    .property_flags
                    .contains(required_flags)
        });
        let memory_type = match memory_type {
            Some(index) => index,
            None => {
                unsafe { self.device.destroy_buffer(buffer, None) };
                return Err("no host-visible coherent Vulkan upload memory type exists".into());
            }
        };
        let allocate_info = vk::MemoryAllocateInfo::default()
            .allocation_size(requirements.size)
            .memory_type_index(memory_type);
        let memory = match unsafe { self.device.allocate_memory(&allocate_info, None) } {
            Ok(memory) => memory,
            Err(error) => {
                unsafe { self.device.destroy_buffer(buffer, None) };
                return Err(format!("vkAllocateMemory failed: {error:?}"));
            }
        };
        if let Err(error) = unsafe { self.device.bind_buffer_memory(buffer, memory, 0) } {
            unsafe {
                self.device.free_memory(memory, None);
                self.device.destroy_buffer(buffer, None);
            }
            return Err(format!("vkBindBufferMemory failed: {error:?}"));
        }
        let mapped = match unsafe {
            self.device
                .map_memory(memory, 0, size, vk::MemoryMapFlags::empty())
        } {
            Ok(mapped) => mapped.cast::<u8>(),
            Err(error) => {
                unsafe {
                    self.device.free_memory(memory, None);
                    self.device.destroy_buffer(buffer, None);
                }
                return Err(format!("vkMapMemory failed: {error:?}"));
            }
        };
        Ok(StagingBuffer {
            buffer,
            memory,
            mapped,
            len,
        })
    }

    unsafe fn free_staging_buffer(&self, staging: &mut StagingBuffer) {
        if !staging.mapped.is_null() && staging.memory != vk::DeviceMemory::null() {
            unsafe { self.device.unmap_memory(staging.memory) };
        }
        if staging.buffer != vk::Buffer::null() {
            unsafe { self.device.destroy_buffer(staging.buffer, None) };
        }
        if staging.memory != vk::DeviceMemory::null() {
            unsafe { self.device.free_memory(staging.memory, None) };
        }
        *staging = StagingBuffer::default();
    }

    unsafe fn recreate_swapchain(
        &mut self,
        requested_width: u32,
        requested_height: u32,
        vsync: bool,
    ) -> Result<VulkanStatus, String> {
        unsafe { self.device.device_wait_idle() }
            .map_err(|error| format!("vkDeviceWaitIdle failed during resize: {error:?}"))?;
        let capabilities = unsafe {
            self.surface_loader
                .get_physical_device_surface_capabilities(self.physical_device, self.surface)
        }
        .map_err(|error| format!("surface-capabilities query failed: {error:?}"))?;
        if !capabilities
            .supported_usage_flags
            .contains(vk::ImageUsageFlags::TRANSFER_DST)
        {
            return Err("the Vulkan swapchain does not support transfer-destination images".into());
        }
        let format = unsafe { self.choose_surface_format() }?;
        let present_modes = unsafe {
            self.surface_loader
                .get_physical_device_surface_present_modes(self.physical_device, self.surface)
        }
        .map_err(|error| format!("present-mode query failed: {error:?}"))?;
        let present_mode = if vsync {
            vk::PresentModeKHR::FIFO
        } else if present_modes.contains(&vk::PresentModeKHR::MAILBOX) {
            vk::PresentModeKHR::MAILBOX
        } else {
            vk::PresentModeKHR::FIFO
        };
        let extent = if capabilities.current_extent.width != u32::MAX {
            capabilities.current_extent
        } else {
            vk::Extent2D {
                width: requested_width.clamp(
                    capabilities.min_image_extent.width,
                    capabilities.max_image_extent.width,
                ),
                height: requested_height.clamp(
                    capabilities.min_image_extent.height,
                    capabilities.max_image_extent.height,
                ),
            }
        };
        if extent.width == 0 || extent.height == 0 {
            self.swapchain_dirty = true;
            return Ok(VulkanStatus::Minimized);
        }
        let desired_images = capabilities.min_image_count.saturating_add(1);
        let image_count = if capabilities.max_image_count != 0 {
            desired_images.min(capabilities.max_image_count)
        } else {
            desired_images
        };
        let composite_alpha = [
            vk::CompositeAlphaFlagsKHR::OPAQUE,
            vk::CompositeAlphaFlagsKHR::PRE_MULTIPLIED,
            vk::CompositeAlphaFlagsKHR::POST_MULTIPLIED,
            vk::CompositeAlphaFlagsKHR::INHERIT,
        ]
        .into_iter()
        .find(|mode| capabilities.supported_composite_alpha.contains(*mode))
        .ok_or_else(|| "the Vulkan surface reported no composite-alpha mode".to_string())?;
        let create_info = vk::SwapchainCreateInfoKHR::default()
            .surface(self.surface)
            .min_image_count(image_count)
            .image_format(format.format)
            .image_color_space(format.color_space)
            .image_extent(extent)
            .image_array_layers(1)
            .image_usage(vk::ImageUsageFlags::TRANSFER_DST)
            .image_sharing_mode(vk::SharingMode::EXCLUSIVE)
            .pre_transform(capabilities.current_transform)
            .composite_alpha(composite_alpha)
            .present_mode(present_mode)
            .clipped(true)
            .old_swapchain(self.swapchain);
        let new_swapchain = match unsafe { self.swapchain_loader.create_swapchain(&create_info, None) } {
            Ok(swapchain) => swapchain,
            Err(vk::Result::ERROR_OUT_OF_DATE_KHR) => {
                self.swapchain_dirty = true;
                return Ok(VulkanStatus::SwapchainOutOfDate);
            }
            Err(error) => return Err(format!("vkCreateSwapchainKHR failed: {error:?}")),
        };
        let new_images = match unsafe { self.swapchain_loader.get_swapchain_images(new_swapchain) }
        {
            Ok(images) => images,
            Err(error) => {
                unsafe { self.swapchain_loader.destroy_swapchain(new_swapchain, None) };
                return Err(format!("vkGetSwapchainImagesKHR failed: {error:?}"));
            }
        };
        if self.swapchain != vk::SwapchainKHR::null() {
            unsafe {
                self.swapchain_loader
                    .destroy_swapchain(self.swapchain, None)
            };
        }
        self.swapchain = new_swapchain;
        self.swapchain_images = new_images;
        for semaphore in self.present_semaphores.drain(..) {
            unsafe { self.device.destroy_semaphore(semaphore, None) };
        }
        self.present_mode = present_mode;
        for _ in &self.swapchain_images {
            self.present_semaphores.push(
                unsafe {
                    self.device
                        .create_semaphore(&vk::SemaphoreCreateInfo::default(), None)
                }
                .map_err(|error| format!("create per-image present semaphore: {error:?}"))?,
            );
        }
        self.image_initialized = vec![false; self.swapchain_images.len()];
        self.swapchain_format = format.format;
        self.swapchain_extent = extent;
        self.requested_extent = vk::Extent2D {
            width: requested_width,
            height: requested_height,
        };
        self.vsync = vsync;
        self.swapchain_dirty = false;
        self.current_frame = 0;
        Ok(VulkanStatus::Ok)
    }

    unsafe fn recover_signaled_fence(&mut self, frame_index: usize) {
        let _ = unsafe { self.device.device_wait_idle() };
        let frame = &mut self.frames[frame_index];
        if frame.fence != vk::Fence::null() {
            unsafe { self.device.destroy_fence(frame.fence, None) };
        }
        let info = vk::FenceCreateInfo::default().flags(vk::FenceCreateFlags::SIGNALED);
        frame.fence = unsafe { self.device.create_fence(&info, None) }.unwrap_or(vk::Fence::null());
    }
}

impl Drop for VulkanRenderer {
    fn drop(&mut self) {
        unsafe {
            let _ = self.device.device_wait_idle();
            if let Some(mut scene) = self.scene.take() {
                scene.destroy(self);
            }
            for semaphore in self.present_semaphores.drain(..) {
                self.device.destroy_semaphore(semaphore, None);
            }
            for frame in std::mem::take(&mut self.frames) {
                if frame.fence != vk::Fence::null() {
                    self.device.destroy_fence(frame.fence, None);
                }
                if frame.image_available != vk::Semaphore::null() {
                    self.device.destroy_semaphore(frame.image_available, None);
                }
            }
            if self.command_pool != vk::CommandPool::null() {
                self.device.destroy_command_pool(self.command_pool, None);
            }
            if self.swapchain != vk::SwapchainKHR::null() {
                self.swapchain_loader
                    .destroy_swapchain(self.swapchain, None);
            }
            self.device.destroy_device(None);
            self.surface_loader.destroy_surface(self.surface, None);
            self.instance.destroy_instance(None);
        }
    }
}

/// Opaque handle exposed to C++.
pub struct RsVulkanRenderer {
    inner: VulkanRenderer,
}

#[no_mangle]
pub unsafe extern "C" fn rs_vulkan_create(
    hwnd: *mut c_void,
    out_renderer: *mut *mut RsVulkanRenderer,
) -> i32 {
    if out_renderer.is_null() {
        set_last_error("rs_vulkan_create received a null output pointer");
        return RS_ERR_BAD_ARG;
    }
    unsafe { *out_renderer = ptr::null_mut() };
    match catch_unwind(AssertUnwindSafe(|| unsafe { VulkanRenderer::new(hwnd) })) {
        Ok(Ok(inner)) => {
            let renderer = Box::new(RsVulkanRenderer { inner });
            unsafe { *out_renderer = Box::into_raw(renderer) };
            set_last_error("");
            RS_OK
        }
        Ok(Err(error)) => {
            set_last_error(error);
            RS_ERR_VULKAN_UNAVAILABLE
        }
        Err(_) => {
            set_last_error("panic while creating the Vulkan renderer");
            RS_ERR_PANIC
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn rs_vulkan_destroy(renderer: *mut RsVulkanRenderer) {
    if renderer.is_null() {
        return;
    }
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        drop(Box::from_raw(renderer));
    }));
}

#[no_mangle]
pub unsafe extern "C" fn rs_vulkan_prepare(
    renderer: *mut RsVulkanRenderer,
    target_width: i32,
    target_height: i32,
    vsync: i32,
) -> i32 {
    if renderer.is_null() || target_width <= 0 || target_height <= 0 {
        set_last_error("rs_vulkan_prepare received invalid swapchain dimensions");
        return RS_ERR_BAD_ARG;
    }
    let result = catch_unwind(AssertUnwindSafe(|| unsafe {
        (&mut *renderer).inner.recreate_swapchain(
            target_width as u32,
            target_height as u32,
            vsync != 0,
        )
    }));
    match result {
        Ok(Ok(VulkanStatus::Ok)) => {
            set_last_error("");
            RS_OK
        }
        Ok(Ok(VulkanStatus::Minimized)) => {
            set_last_error("");
            RS_VULKAN_MINIMIZED
        }
        Ok(Ok(VulkanStatus::SwapchainOutOfDate)) => {
            set_last_error("");
            RS_VULKAN_SWAPCHAIN_OUT_OF_DATE
        }
        Ok(Err(error)) => {
            set_last_error(error);
            RS_ERR_VULKAN_RUNTIME
        }
        Err(_) => {
            set_last_error("panic while preparing the Vulkan swapchain");
            RS_ERR_PANIC
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn rs_vulkan_last_error(dst: *mut c_char, dst_cap: usize) -> usize {
    LAST_ERROR.with(|slot| {
        let message = slot.borrow();
        let bytes = message.as_bytes();
        if !dst.is_null() && dst_cap != 0 {
            let copy_len = bytes.len().min(dst_cap - 1);
            unsafe {
                ptr::copy_nonoverlapping(bytes.as_ptr(), dst.cast::<u8>(), copy_len);
                *dst.add(copy_len) = 0;
            }
        }
        bytes.len()
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn creates_vulkan_instance_with_win32_surface_extensions() {
        unsafe {
            let entry = Entry::load().expect("load vulkan-1.dll");
            let application_name = CString::new("mission-editor-vulkan-test").unwrap();
            let application_info = vk::ApplicationInfo::default()
                .application_name(&application_name)
                .api_version(vk::API_VERSION_1_0);
            let extensions = [
                ash::khr::surface::NAME.as_ptr(),
                ash::khr::win32_surface::NAME.as_ptr(),
            ];
            let create_info = vk::InstanceCreateInfo::default()
                .application_info(&application_info)
                .enabled_extension_names(&extensions);
            let instance = entry
                .create_instance(&create_info, None)
                .expect("create Vulkan instance");
            instance.destroy_instance(None);
        }
    }
}
