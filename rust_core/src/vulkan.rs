//! Vulkan presentation for the legacy CPU map rasterizer.
//!
//! Rust owns the Vulkan instance, device, Win32 surface, swapchain,
//! synchronization objects and persistently mapped upload memory. The C++
//! renderer only lends this module a read-only view of its final pixels.

use ash::{vk, Entry};
use std::cell::RefCell;
use std::ffi::{c_char, c_void, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::slice;

const RS_OK: i32 = 0;
const RS_ERR_BAD_ARG: i32 = -1;
const RS_ERR_PANIC: i32 = -3;
const RS_ERR_VULKAN_UNAVAILABLE: i32 = -10;
const RS_ERR_VULKAN_RUNTIME: i32 = -11;
const MAX_FRAMES_IN_FLIGHT: usize = 2;

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

struct UploadImage {
    image: vk::Image,
    memory: vk::DeviceMemory,
    extent: vk::Extent2D,
}

impl Default for UploadImage {
    fn default() -> Self {
        Self {
            image: vk::Image::null(),
            memory: vk::DeviceMemory::null(),
            extent: vk::Extent2D::default(),
        }
    }
}

struct FrameResources {
    command_buffer: vk::CommandBuffer,
    image_available: vk::Semaphore,
    upload_complete: vk::Semaphore,
    fence: vk::Fence,
    staging: StagingBuffer,
    upload_image: UploadImage,
    source_x_offsets: Vec<isize>,
}

impl FrameResources {
    fn new(command_buffer: vk::CommandBuffer) -> Self {
        Self {
            command_buffer,
            image_available: vk::Semaphore::null(),
            upload_complete: vk::Semaphore::null(),
            fence: vk::Fence::null(),
            staging: StagingBuffer::default(),
            upload_image: UploadImage::default(),
            source_x_offsets: Vec::new(),
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
    image_initialized: Vec<bool>,
    swapchain_format: vk::Format,
    swapchain_opaque: bool,
    gpu_bgrx_blit_supported: bool,
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
            image_initialized: Vec::new(),
            swapchain_format: vk::Format::UNDEFINED,
            swapchain_opaque: false,
            gpu_bgrx_blit_supported: false,
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
                if !properties.queue_flags.contains(vk::QueueFlags::GRAPHICS) {
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
                "no Vulkan device supports graphics, presentation and VK_KHR_swapchain".into()
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
            frame.upload_complete = unsafe { self.device.create_semaphore(&semaphore_info, None) }
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
            .usage(vk::BufferUsageFlags::TRANSFER_SRC)
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

    unsafe fn create_upload_image(&self, extent: vk::Extent2D) -> Result<UploadImage, String> {
        let image_info = vk::ImageCreateInfo::default()
            .image_type(vk::ImageType::TYPE_2D)
            .format(vk::Format::B8G8R8A8_UNORM)
            .extent(vk::Extent3D {
                width: extent.width,
                height: extent.height,
                depth: 1,
            })
            .mip_levels(1)
            .array_layers(1)
            .samples(vk::SampleCountFlags::TYPE_1)
            .tiling(vk::ImageTiling::OPTIMAL)
            .usage(vk::ImageUsageFlags::TRANSFER_DST | vk::ImageUsageFlags::TRANSFER_SRC)
            .sharing_mode(vk::SharingMode::EXCLUSIVE)
            .initial_layout(vk::ImageLayout::UNDEFINED);
        let image = unsafe { self.device.create_image(&image_info, None) }
            .map_err(|error| format!("vkCreateImage for the upload frame failed: {error:?}"))?;
        let requirements = unsafe { self.device.get_image_memory_requirements(image) };
        let memory_properties = unsafe {
            self.instance
                .get_physical_device_memory_properties(self.physical_device)
        };
        let memory_type = (0..memory_properties.memory_type_count).find(|index| {
            requirements.memory_type_bits & (1_u32 << index) != 0
                && memory_properties.memory_types[*index as usize]
                    .property_flags
                    .contains(vk::MemoryPropertyFlags::DEVICE_LOCAL)
        });
        let memory_type = match memory_type {
            Some(index) => index,
            None => {
                unsafe { self.device.destroy_image(image, None) };
                return Err("no device-local Vulkan upload-image memory type exists".into());
            }
        };
        let allocate_info = vk::MemoryAllocateInfo::default()
            .allocation_size(requirements.size)
            .memory_type_index(memory_type);
        let memory = match unsafe { self.device.allocate_memory(&allocate_info, None) } {
            Ok(memory) => memory,
            Err(error) => {
                unsafe { self.device.destroy_image(image, None) };
                return Err(format!(
                    "vkAllocateMemory for the upload image failed: {error:?}"
                ));
            }
        };
        if let Err(error) = unsafe { self.device.bind_image_memory(image, memory, 0) } {
            unsafe {
                self.device.free_memory(memory, None);
                self.device.destroy_image(image, None);
            }
            return Err(format!("vkBindImageMemory failed: {error:?}"));
        }
        Ok(UploadImage {
            image,
            memory,
            extent,
        })
    }

    unsafe fn free_upload_image(&self, upload: &mut UploadImage) {
        if upload.image != vk::Image::null() {
            unsafe { self.device.destroy_image(upload.image, None) };
        }
        if upload.memory != vk::DeviceMemory::null() {
            unsafe { self.device.free_memory(upload.memory, None) };
        }
        *upload = UploadImage::default();
    }

    unsafe fn ensure_gpu_upload_resources(
        &mut self,
        frame_index: usize,
        extent: vk::Extent2D,
        staging_len: usize,
    ) -> Result<(), String> {
        if self.frames[frame_index].staging.len < staging_len {
            let old_len = self.frames[frame_index].staging.len;
            let growth_target = old_len.saturating_add(old_len / 2).max(staging_len);
            let new_staging = unsafe { self.create_staging_buffer(growth_target) }?;
            let mut old_staging =
                std::mem::replace(&mut self.frames[frame_index].staging, new_staging);
            unsafe { self.free_staging_buffer(&mut old_staging) };
        }

        if self.frames[frame_index].upload_image.extent != extent {
            let new_upload = unsafe { self.create_upload_image(extent) }?;
            let mut old_upload =
                std::mem::replace(&mut self.frames[frame_index].upload_image, new_upload);
            unsafe { self.free_upload_image(&mut old_upload) };
        }
        Ok(())
    }

    unsafe fn supports_gpu_bgrx_blit(&self) -> bool {
        if !self.swapchain_opaque
            || !matches!(
                self.swapchain_format,
                vk::Format::B8G8R8A8_UNORM | vk::Format::R8G8B8A8_UNORM
            )
        {
            return false;
        }
        let source = unsafe {
            self.instance.get_physical_device_format_properties(
                self.physical_device,
                vk::Format::B8G8R8A8_UNORM,
            )
        };
        let destination = unsafe {
            self.instance
                .get_physical_device_format_properties(self.physical_device, self.swapchain_format)
        };
        source
            .optimal_tiling_features
            .contains(vk::FormatFeatureFlags::BLIT_SRC)
            && destination
                .optimal_tiling_features
                .contains(vk::FormatFeatureFlags::BLIT_DST)
    }

    unsafe fn recreate_swapchain(
        &mut self,
        requested_width: u32,
        requested_height: u32,
        vsync: bool,
    ) -> Result<(), String> {
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
        } else if present_modes.contains(&vk::PresentModeKHR::IMMEDIATE) {
            vk::PresentModeKHR::IMMEDIATE
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
            return Err("cannot create a Vulkan swapchain for a zero-sized window".into());
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
        let new_swapchain = unsafe { self.swapchain_loader.create_swapchain(&create_info, None) }
            .map_err(|error| format!("vkCreateSwapchainKHR failed: {error:?}"))?;
        let new_images = match unsafe { self.swapchain_loader.get_swapchain_images(new_swapchain) }
        {
            Ok(images) => images,
            Err(error) => {
                unsafe { self.swapchain_loader.destroy_swapchain(new_swapchain, None) };
                return Err(format!("vkGetSwapchainImagesKHR failed: {error:?}"));
            }
        };
        let staging_len = (extent.width as usize)
            .checked_mul(extent.height as usize)
            .and_then(|pixels| pixels.checked_mul(4))
            .ok_or_else(|| "Vulkan upload buffer size overflow".to_string())?;
        // Window drags can generate dozens of sizes. Keep the mapped upload
        // allocation when it is already large enough instead of churning a
        // Win32 address-space mapping for every swapchain recreation.
        for frame_index in 0..self.frames.len() {
            let old_len = self.frames[frame_index].staging.len;
            if old_len >= staging_len {
                continue;
            }
            let growth_target = old_len.saturating_add(old_len / 2).max(staging_len);
            let new_staging = match unsafe { self.create_staging_buffer(growth_target) } {
                Ok(staging) => staging,
                Err(error) => {
                    unsafe { self.swapchain_loader.destroy_swapchain(new_swapchain, None) };
                    return Err(error);
                }
            };
            let mut old_staging =
                std::mem::replace(&mut self.frames[frame_index].staging, new_staging);
            unsafe { self.free_staging_buffer(&mut old_staging) };
        }
        if self.swapchain != vk::SwapchainKHR::null() {
            unsafe {
                self.swapchain_loader
                    .destroy_swapchain(self.swapchain, None)
            };
        }
        self.swapchain = new_swapchain;
        self.swapchain_images = new_images;
        self.image_initialized = vec![false; self.swapchain_images.len()];
        self.swapchain_format = format.format;
        self.swapchain_opaque = composite_alpha == vk::CompositeAlphaFlagsKHR::OPAQUE;
        self.gpu_bgrx_blit_supported = unsafe { self.supports_gpu_bgrx_blit() };
        self.swapchain_extent = extent;
        self.requested_extent = vk::Extent2D {
            width: requested_width,
            height: requested_height,
        };
        self.vsync = vsync;
        self.swapchain_dirty = false;
        self.current_frame = 0;
        Ok(())
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

    #[allow(clippy::too_many_arguments)]
    unsafe fn present(
        &mut self,
        pixels: &[u8],
        surface_width: usize,
        surface_height: usize,
        pitch: usize,
        bytes_per_pixel: usize,
        red_mask: u32,
        green_mask: u32,
        blue_mask: u32,
        src_left: i32,
        src_top: i32,
        src_width: usize,
        src_height: usize,
        target_width: u32,
        target_height: u32,
        vsync: bool,
    ) -> Result<(), String> {
        if target_width == 0 || target_height == 0 {
            return Ok(());
        }
        let requested = vk::Extent2D {
            width: target_width,
            height: target_height,
        };
        if self.swapchain == vk::SwapchainKHR::null()
            || self.swapchain_dirty
            || self.requested_extent != requested
            || self.vsync != vsync
        {
            unsafe { self.recreate_swapchain(target_width, target_height, vsync) }?;
        }
        let frame_index = self.current_frame;
        let frame_fence = self.frames[frame_index].fence;
        let image_available = self.frames[frame_index].image_available;
        let upload_complete = self.frames[frame_index].upload_complete;
        let command_buffer = self.frames[frame_index].command_buffer;
        unsafe { self.device.wait_for_fences(&[frame_fence], true, u64::MAX) }
            .map_err(|error| format!("vkWaitForFences failed: {error:?}"))?;

        let acquire = unsafe {
            self.swapchain_loader.acquire_next_image(
                self.swapchain,
                u64::MAX,
                image_available,
                vk::Fence::null(),
            )
        };
        let (image_index, acquire_suboptimal) = match acquire {
            Ok(result) => result,
            Err(vk::Result::ERROR_OUT_OF_DATE_KHR) => {
                unsafe { self.recreate_swapchain(target_width, target_height, vsync) }?;
                unsafe {
                    self.swapchain_loader.acquire_next_image(
                        self.swapchain,
                        u64::MAX,
                        image_available,
                        vk::Fence::null(),
                    )
                }
                .map_err(|error| format!("image acquisition after resize failed: {error:?}"))?
            }
            Err(error) => return Err(format!("vkAcquireNextImageKHR failed: {error:?}")),
        };

        let source_is_bgrx8888 = bytes_per_pixel == 4
            && red_mask == 0x00ff_0000
            && green_mask == 0x0000_ff00
            && blue_mask == 0x0000_00ff;
        let source_rect_is_inside = src_left >= 0
            && src_top >= 0
            && (src_left as usize).saturating_add(src_width) <= surface_width
            && (src_top as usize).saturating_add(src_height) <= surface_height;
        let source_extent = vk::Extent2D {
            width: u32::try_from(src_width).map_err(|_| "source rectangle is too wide")?,
            height: u32::try_from(src_height).map_err(|_| "source rectangle is too tall")?,
        };
        let source_upload_len = src_width
            .checked_mul(src_height)
            .and_then(|pixels| pixels.checked_mul(4))
            .ok_or_else(|| "Vulkan source upload size overflow".to_string())?;
        let use_direct_bgrx_upload = source_is_bgrx8888
            && source_rect_is_inside
            && self.swapchain_opaque
            && matches!(
                self.swapchain_format,
                vk::Format::B8G8R8A8_UNORM | vk::Format::B8G8R8A8_SRGB
            )
            && source_extent == self.swapchain_extent;
        let mut use_gpu_blit = !use_direct_bgrx_upload
            && source_is_bgrx8888
            && source_rect_is_inside
            && self.gpu_bgrx_blit_supported;
        if use_gpu_blit
            && unsafe {
                self.ensure_gpu_upload_resources(frame_index, source_extent, source_upload_len)
            }
            .is_err()
        {
            // Device-local image allocation is an optimization. The mapped
            // CPU conversion path remains available on constrained drivers.
            use_gpu_blit = false;
            self.gpu_bgrx_blit_supported = false;
        }

        let staging_mapped = self.frames[frame_index].staging.mapped;
        let staging_len = self.frames[frame_index].staging.len;
        let destination = unsafe { slice::from_raw_parts_mut(staging_mapped, staging_len) };
        if use_direct_bgrx_upload || use_gpu_blit {
            copy_bgrx_source_rect(
                destination,
                pixels,
                surface_width,
                surface_height,
                pitch,
                src_left as usize,
                src_top as usize,
                src_width,
                src_height,
            )?;
        } else {
            write_scaled_pixels(
                destination,
                self.swapchain_extent.width as usize,
                self.swapchain_extent.height as usize,
                self.swapchain_format,
                pixels,
                surface_width,
                surface_height,
                pitch,
                bytes_per_pixel,
                red_mask,
                green_mask,
                blue_mask,
                src_left,
                src_top,
                src_width,
                src_height,
                &mut self.frames[frame_index].source_x_offsets,
            )?;
        }

        unsafe { self.device.reset_fences(&[frame_fence]) }
            .map_err(|error| format!("vkResetFences failed: {error:?}"))?;
        unsafe {
            self.device
                .reset_command_buffer(command_buffer, vk::CommandBufferResetFlags::empty())
        }
        .map_err(|error| format!("vkResetCommandBuffer failed: {error:?}"))?;
        let begin_info = vk::CommandBufferBeginInfo::default()
            .flags(vk::CommandBufferUsageFlags::ONE_TIME_SUBMIT);
        unsafe {
            self.device
                .begin_command_buffer(command_buffer, &begin_info)
        }
        .map_err(|error| format!("vkBeginCommandBuffer failed: {error:?}"))?;

        let old_layout = if self.image_initialized[image_index as usize] {
            vk::ImageLayout::PRESENT_SRC_KHR
        } else {
            vk::ImageLayout::UNDEFINED
        };
        let to_transfer = vk::ImageMemoryBarrier::default()
            .old_layout(old_layout)
            .new_layout(vk::ImageLayout::TRANSFER_DST_OPTIMAL)
            .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
            .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
            .image(self.swapchain_images[image_index as usize])
            .subresource_range(
                vk::ImageSubresourceRange::default()
                    .aspect_mask(vk::ImageAspectFlags::COLOR)
                    .level_count(1)
                    .layer_count(1),
            )
            .src_access_mask(vk::AccessFlags::empty())
            .dst_access_mask(vk::AccessFlags::TRANSFER_WRITE);
        unsafe {
            self.device.cmd_pipeline_barrier(
                command_buffer,
                vk::PipelineStageFlags::TOP_OF_PIPE,
                vk::PipelineStageFlags::TRANSFER,
                vk::DependencyFlags::empty(),
                &[],
                &[],
                &[to_transfer],
            )
        };
        if use_gpu_blit {
            let upload_image = self.frames[frame_index].upload_image.image;
            let upload_to_destination = vk::ImageMemoryBarrier::default()
                .old_layout(vk::ImageLayout::UNDEFINED)
                .new_layout(vk::ImageLayout::TRANSFER_DST_OPTIMAL)
                .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
                .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
                .image(upload_image)
                .subresource_range(
                    vk::ImageSubresourceRange::default()
                        .aspect_mask(vk::ImageAspectFlags::COLOR)
                        .level_count(1)
                        .layer_count(1),
                )
                .src_access_mask(vk::AccessFlags::empty())
                .dst_access_mask(vk::AccessFlags::TRANSFER_WRITE);
            unsafe {
                self.device.cmd_pipeline_barrier(
                    command_buffer,
                    vk::PipelineStageFlags::TOP_OF_PIPE,
                    vk::PipelineStageFlags::TRANSFER,
                    vk::DependencyFlags::empty(),
                    &[],
                    &[],
                    &[upload_to_destination],
                )
            };
            let upload_region = vk::BufferImageCopy::default()
                .buffer_offset(0)
                .buffer_row_length(0)
                .buffer_image_height(0)
                .image_subresource(
                    vk::ImageSubresourceLayers::default()
                        .aspect_mask(vk::ImageAspectFlags::COLOR)
                        .layer_count(1),
                )
                .image_extent(vk::Extent3D {
                    width: source_extent.width,
                    height: source_extent.height,
                    depth: 1,
                });
            unsafe {
                self.device.cmd_copy_buffer_to_image(
                    command_buffer,
                    self.frames[frame_index].staging.buffer,
                    upload_image,
                    vk::ImageLayout::TRANSFER_DST_OPTIMAL,
                    &[upload_region],
                )
            };
            let upload_to_source = vk::ImageMemoryBarrier::default()
                .old_layout(vk::ImageLayout::TRANSFER_DST_OPTIMAL)
                .new_layout(vk::ImageLayout::TRANSFER_SRC_OPTIMAL)
                .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
                .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
                .image(upload_image)
                .subresource_range(
                    vk::ImageSubresourceRange::default()
                        .aspect_mask(vk::ImageAspectFlags::COLOR)
                        .level_count(1)
                        .layer_count(1),
                )
                .src_access_mask(vk::AccessFlags::TRANSFER_WRITE)
                .dst_access_mask(vk::AccessFlags::TRANSFER_READ);
            unsafe {
                self.device.cmd_pipeline_barrier(
                    command_buffer,
                    vk::PipelineStageFlags::TRANSFER,
                    vk::PipelineStageFlags::TRANSFER,
                    vk::DependencyFlags::empty(),
                    &[],
                    &[],
                    &[upload_to_source],
                )
            };
            let source_offsets = [
                vk::Offset3D { x: 0, y: 0, z: 0 },
                vk::Offset3D {
                    x: source_extent.width as i32,
                    y: source_extent.height as i32,
                    z: 1,
                },
            ];
            let destination_offsets = [
                vk::Offset3D { x: 0, y: 0, z: 0 },
                vk::Offset3D {
                    x: self.swapchain_extent.width as i32,
                    y: self.swapchain_extent.height as i32,
                    z: 1,
                },
            ];
            let blit_region = vk::ImageBlit::default()
                .src_subresource(
                    vk::ImageSubresourceLayers::default()
                        .aspect_mask(vk::ImageAspectFlags::COLOR)
                        .layer_count(1),
                )
                .src_offsets(source_offsets)
                .dst_subresource(
                    vk::ImageSubresourceLayers::default()
                        .aspect_mask(vk::ImageAspectFlags::COLOR)
                        .layer_count(1),
                )
                .dst_offsets(destination_offsets);
            unsafe {
                self.device.cmd_blit_image(
                    command_buffer,
                    upload_image,
                    vk::ImageLayout::TRANSFER_SRC_OPTIMAL,
                    self.swapchain_images[image_index as usize],
                    vk::ImageLayout::TRANSFER_DST_OPTIMAL,
                    &[blit_region],
                    vk::Filter::NEAREST,
                )
            };
        } else {
            let copy_region = vk::BufferImageCopy::default()
                .buffer_offset(0)
                .buffer_row_length(0)
                .buffer_image_height(0)
                .image_subresource(
                    vk::ImageSubresourceLayers::default()
                        .aspect_mask(vk::ImageAspectFlags::COLOR)
                        .layer_count(1),
                )
                .image_extent(vk::Extent3D {
                    width: self.swapchain_extent.width,
                    height: self.swapchain_extent.height,
                    depth: 1,
                });
            unsafe {
                self.device.cmd_copy_buffer_to_image(
                    command_buffer,
                    self.frames[frame_index].staging.buffer,
                    self.swapchain_images[image_index as usize],
                    vk::ImageLayout::TRANSFER_DST_OPTIMAL,
                    &[copy_region],
                )
            };
        }
        let to_present = vk::ImageMemoryBarrier::default()
            .old_layout(vk::ImageLayout::TRANSFER_DST_OPTIMAL)
            .new_layout(vk::ImageLayout::PRESENT_SRC_KHR)
            .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
            .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
            .image(self.swapchain_images[image_index as usize])
            .subresource_range(
                vk::ImageSubresourceRange::default()
                    .aspect_mask(vk::ImageAspectFlags::COLOR)
                    .level_count(1)
                    .layer_count(1),
            )
            .src_access_mask(vk::AccessFlags::TRANSFER_WRITE)
            .dst_access_mask(vk::AccessFlags::empty());
        unsafe {
            self.device.cmd_pipeline_barrier(
                command_buffer,
                vk::PipelineStageFlags::TRANSFER,
                vk::PipelineStageFlags::BOTTOM_OF_PIPE,
                vk::DependencyFlags::empty(),
                &[],
                &[],
                &[to_present],
            )
        };
        unsafe { self.device.end_command_buffer(command_buffer) }
            .map_err(|error| format!("vkEndCommandBuffer failed: {error:?}"))?;

        let wait_semaphores = [image_available];
        let wait_stages = [vk::PipelineStageFlags::TRANSFER];
        let command_buffers = [command_buffer];
        let signal_semaphores = [upload_complete];
        let submit_info = [vk::SubmitInfo::default()
            .wait_semaphores(&wait_semaphores)
            .wait_dst_stage_mask(&wait_stages)
            .command_buffers(&command_buffers)
            .signal_semaphores(&signal_semaphores)];
        if let Err(error) = unsafe {
            self.device
                .queue_submit(self.queue, &submit_info, frame_fence)
        } {
            unsafe { self.recover_signaled_fence(frame_index) };
            return Err(format!("vkQueueSubmit failed: {error:?}"));
        }
        self.current_frame = (frame_index + 1) % self.frames.len();
        self.image_initialized[image_index as usize] = true;
        let swapchains = [self.swapchain];
        let image_indices = [image_index];
        let present_info = vk::PresentInfoKHR::default()
            .wait_semaphores(&signal_semaphores)
            .swapchains(&swapchains)
            .image_indices(&image_indices);
        match unsafe {
            self.swapchain_loader
                .queue_present(self.queue, &present_info)
        } {
            Ok(present_suboptimal) => {
                self.swapchain_dirty = acquire_suboptimal || present_suboptimal;
                Ok(())
            }
            Err(vk::Result::ERROR_OUT_OF_DATE_KHR) => {
                self.swapchain_dirty = true;
                Ok(())
            }
            Err(error) => Err(format!("vkQueuePresentKHR failed: {error:?}")),
        }
    }
}

impl Drop for VulkanRenderer {
    fn drop(&mut self) {
        unsafe {
            let _ = self.device.device_wait_idle();
            for mut frame in std::mem::take(&mut self.frames) {
                self.free_staging_buffer(&mut frame.staging);
                self.free_upload_image(&mut frame.upload_image);
                if frame.fence != vk::Fence::null() {
                    self.device.destroy_fence(frame.fence, None);
                }
                if frame.upload_complete != vk::Semaphore::null() {
                    self.device.destroy_semaphore(frame.upload_complete, None);
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

#[derive(Clone, Copy)]
struct ComponentDecoder {
    mask: u32,
    shift: u32,
    maximum: u64,
}

impl ComponentDecoder {
    fn new(mask: u32) -> Self {
        let bits = mask.count_ones();
        Self {
            mask,
            shift: mask.trailing_zeros(),
            maximum: if bits == 32 {
                u32::MAX as u64
            } else if bits == 0 {
                0
            } else {
                (1_u64 << bits) - 1
            },
        }
    }

    #[inline(always)]
    fn decode(self, pixel: u32) -> u8 {
        if self.maximum == 0 {
            return 0;
        }
        let value = ((pixel & self.mask) >> self.shift) as u64;
        ((value * 255 + self.maximum / 2) / self.maximum) as u8
    }
}

#[allow(clippy::too_many_arguments)]
fn copy_bgrx_source_rect(
    destination: &mut [u8],
    source: &[u8],
    source_width: usize,
    source_height: usize,
    source_pitch: usize,
    source_left: usize,
    source_top: usize,
    source_rect_width: usize,
    source_rect_height: usize,
) -> Result<(), String> {
    let row_bytes = source_rect_width
        .checked_mul(4)
        .ok_or_else(|| "source row byte count overflow".to_string())?;
    let required = row_bytes
        .checked_mul(source_rect_height)
        .ok_or_else(|| "source rectangle byte count overflow".to_string())?;
    let minimum_pitch = source_width
        .checked_mul(4)
        .ok_or_else(|| "source row size overflow".to_string())?;
    if destination.len() < required
        || source_pitch < minimum_pitch
        || source.len() < source_pitch.saturating_mul(source_height)
        || source_left.saturating_add(source_rect_width) > source_width
        || source_top.saturating_add(source_rect_height) > source_height
    {
        return Err("invalid BGRX source rectangle".into());
    }

    let source_left_bytes = source_left * 4;
    for row in 0..source_rect_height {
        let source_start = (source_top + row) * source_pitch + source_left_bytes;
        let destination_start = row * row_bytes;
        destination[destination_start..destination_start + row_bytes]
            .copy_from_slice(&source[source_start..source_start + row_bytes]);
    }
    Ok(())
}

#[allow(clippy::too_many_arguments)]
fn write_scaled_pixels(
    destination: &mut [u8],
    destination_width: usize,
    destination_height: usize,
    destination_format: vk::Format,
    source: &[u8],
    source_width: usize,
    source_height: usize,
    source_pitch: usize,
    bytes_per_pixel: usize,
    red_mask: u32,
    green_mask: u32,
    blue_mask: u32,
    source_left: i32,
    source_top: i32,
    source_rect_width: usize,
    source_rect_height: usize,
    source_x_offsets: &mut Vec<isize>,
) -> Result<(), String> {
    let required = destination_width
        .checked_mul(destination_height)
        .and_then(|pixels| pixels.checked_mul(4))
        .ok_or_else(|| "destination pixel count overflow".to_string())?;
    if destination.len() < required
        || source_width == 0
        || source_height == 0
        || source_rect_width == 0
        || source_rect_height == 0
        || !(2..=4).contains(&bytes_per_pixel)
    {
        return Err("invalid source or destination pixel buffer".into());
    }
    let minimum_pitch = source_width
        .checked_mul(bytes_per_pixel)
        .ok_or_else(|| "source row size overflow".to_string())?;
    if source_pitch < minimum_pitch || source.len() < source_pitch.saturating_mul(source_height) {
        return Err("source pitch or byte length is too small".into());
    }
    let bgra = matches!(
        destination_format,
        vk::Format::B8G8R8A8_UNORM | vk::Format::B8G8R8A8_SRGB
    );
    if !bgra
        && !matches!(
            destination_format,
            vk::Format::R8G8B8A8_UNORM | vk::Format::R8G8B8A8_SRGB
        )
    {
        return Err("unsupported Vulkan destination pixel format".into());
    }

    // DirectDraw normally exposes the desktop as little-endian B8G8R8X8. This is the
    // editor's hot presentation path, so avoid rebuilding mask metadata and performing
    // three integer divisions for every output pixel. Precomputing the horizontal source
    // offsets also turns nearest-neighbour scaling from one division per pixel into one
    // division per output column.
    let source_is_bgrx8888 = bytes_per_pixel == 4
        && red_mask == 0x00ff_0000
        && green_mask == 0x0000_ff00
        && blue_mask == 0x0000_00ff;
    let source_rect_is_inside = source_left >= 0
        && source_top >= 0
        && (source_left as usize).saturating_add(source_rect_width) <= source_width
        && (source_top as usize).saturating_add(source_rect_height) <= source_height;

    if source_is_bgrx8888
        && source_rect_is_inside
        && source_rect_width == destination_width
        && source_rect_height == destination_height
    {
        let source_left_bytes = source_left as usize * 4;
        for destination_y in 0..destination_height {
            let source_y = source_top as usize + destination_y;
            let source_start = source_y * source_pitch + source_left_bytes;
            let source_row = &source[source_start..source_start + destination_width * 4];
            let output_start = destination_y * destination_width * 4;
            let output_row = &mut destination[output_start..output_start + destination_width * 4];
            if bgra {
                // Copy a complete pixel and only force its unused X byte to opaque. Keeping
                // this as one integer load/OR/store lets LLVM vectorize whole scanlines.
                for (output, input) in output_row
                    .chunks_exact_mut(4)
                    .zip(source_row.chunks_exact(4))
                {
                    let pixel =
                        u32::from_le_bytes([input[0], input[1], input[2], input[3]]) | 0xff00_0000;
                    output.copy_from_slice(&pixel.to_le_bytes());
                }
            } else {
                for (output, input) in output_row
                    .chunks_exact_mut(4)
                    .zip(source_row.chunks_exact(4))
                {
                    output.copy_from_slice(&[input[2], input[1], input[0], 255]);
                }
            }
        }
        return Ok(());
    }

    source_x_offsets.clear();
    source_x_offsets.extend((0..destination_width).map(|destination_x| {
        let source_x = i64::from(source_left)
            + (destination_x as i64 * source_rect_width as i64 / destination_width as i64);
        if source_x < 0 || source_x >= source_width as i64 {
            -1
        } else {
            (source_x as usize * bytes_per_pixel) as isize
        }
    }));

    if source_is_bgrx8888 {
        for destination_y in 0..destination_height {
            let source_y = i64::from(source_top)
                + (destination_y as i64 * source_rect_height as i64 / destination_height as i64);
            let row_start = destination_y * destination_width * 4;
            let output_row = &mut destination[row_start..row_start + destination_width * 4];
            if source_y < 0 || source_y >= source_height as i64 {
                output_row.fill(255);
                continue;
            }

            let source_row = &source[source_y as usize * source_pitch..];
            for (output, source_offset) in output_row
                .chunks_exact_mut(4)
                .zip(source_x_offsets.iter().copied())
            {
                if source_offset < 0 {
                    output.copy_from_slice(&[255, 255, 255, 255]);
                    continue;
                }
                let input = source_offset as usize;
                if bgra {
                    output.copy_from_slice(&[
                        source_row[input],
                        source_row[input + 1],
                        source_row[input + 2],
                        255,
                    ]);
                } else {
                    output.copy_from_slice(&[
                        source_row[input + 2],
                        source_row[input + 1],
                        source_row[input],
                        255,
                    ]);
                }
            }
        }
        return Ok(());
    }

    // Unusual 16/24-bit desktop formats retain the generic conversion path, but the
    // invariant mask metadata is calculated once per frame instead of per channel for
    // every pixel.
    let red_decoder = ComponentDecoder::new(red_mask);
    let green_decoder = ComponentDecoder::new(green_mask);
    let blue_decoder = ComponentDecoder::new(blue_mask);
    for destination_y in 0..destination_height {
        let source_y = i64::from(source_top)
            + (destination_y as i64 * source_rect_height as i64 / destination_height as i64);
        for (destination_x, source_offset) in source_x_offsets.iter().copied().enumerate() {
            let output = (destination_y * destination_width + destination_x) * 4;
            let (red, green, blue) =
                if source_offset < 0 || source_y < 0 || source_y >= source_height as i64 {
                    (255, 255, 255)
                } else {
                    let input = source_y as usize * source_pitch + source_offset as usize;
                    let mut pixel = 0_u32;
                    for byte in 0..bytes_per_pixel {
                        pixel |= u32::from(source[input + byte]) << (byte * 8);
                    }
                    (
                        red_decoder.decode(pixel),
                        green_decoder.decode(pixel),
                        blue_decoder.decode(pixel),
                    )
                };
            if bgra {
                destination[output..output + 4].copy_from_slice(&[blue, green, red, 255]);
            } else {
                destination[output..output + 4].copy_from_slice(&[red, green, blue, 255]);
            }
        }
    }
    Ok(())
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
        Ok(Ok(())) => {
            set_last_error("");
            RS_OK
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
#[allow(clippy::too_many_arguments)]
pub unsafe extern "C" fn rs_vulkan_present(
    renderer: *mut RsVulkanRenderer,
    pixels: *const u8,
    surface_width: i32,
    surface_height: i32,
    pitch: i32,
    bytes_per_pixel: u32,
    red_mask: u32,
    green_mask: u32,
    blue_mask: u32,
    src_left: i32,
    src_top: i32,
    src_width: i32,
    src_height: i32,
    target_width: i32,
    target_height: i32,
    vsync: i32,
) -> i32 {
    if renderer.is_null()
        || pixels.is_null()
        || surface_width <= 0
        || surface_height <= 0
        || pitch <= 0
        || !(2..=4).contains(&bytes_per_pixel)
        || src_width <= 0
        || src_height <= 0
        || target_width < 0
        || target_height < 0
    {
        set_last_error("rs_vulkan_present received invalid pixel-buffer arguments");
        return RS_ERR_BAD_ARG;
    }
    let source_len = match (pitch as usize).checked_mul(surface_height as usize) {
        Some(len) => len,
        None => {
            set_last_error("source pixel-buffer length overflow");
            return RS_ERR_BAD_ARG;
        }
    };
    let result = catch_unwind(AssertUnwindSafe(|| {
        let source = unsafe { slice::from_raw_parts(pixels, source_len) };
        let renderer = unsafe { &mut *renderer };
        unsafe {
            renderer.inner.present(
                source,
                surface_width as usize,
                surface_height as usize,
                pitch as usize,
                bytes_per_pixel as usize,
                red_mask,
                green_mask,
                blue_mask,
                src_left,
                src_top,
                src_width as usize,
                src_height as usize,
                target_width as u32,
                target_height as u32,
                vsync != 0,
            )
        }
    }));
    match result {
        Ok(Ok(())) => {
            set_last_error("");
            RS_OK
        }
        Ok(Err(error)) => {
            set_last_error(error);
            RS_ERR_VULKAN_RUNTIME
        }
        Err(_) => {
            set_last_error("panic while presenting a Vulkan frame");
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

    #[link(name = "user32")]
    unsafe extern "system" {
        fn CreateWindowExW(
            ex_style: u32,
            class_name: *const u16,
            window_name: *const u16,
            style: u32,
            x: i32,
            y: i32,
            width: i32,
            height: i32,
            parent: *mut c_void,
            menu: *mut c_void,
            instance: *mut c_void,
            parameter: *mut c_void,
        ) -> *mut c_void;
        fn DestroyWindow(window: *mut c_void) -> i32;
    }

    fn wide(value: &str) -> Vec<u16> {
        value.encode_utf16().chain(std::iter::once(0)).collect()
    }

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

    #[test]
    fn presents_bgrx_frames_through_gpu_paths() {
        unsafe {
            let class_name = wide("STATIC");
            let window_name = wide("mission-editor-vulkan-test");
            let window = CreateWindowExW(
                0,
                class_name.as_ptr(),
                window_name.as_ptr(),
                0,
                0,
                0,
                64,
                64,
                ptr::null_mut(),
                ptr::null_mut(),
                GetModuleHandleW(ptr::null()),
                ptr::null_mut(),
            );
            assert!(!window.is_null(), "create hidden Vulkan test window");

            let result = VulkanRenderer::new(window).and_then(|mut renderer| {
                let pixels = [
                    0_u8, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0,
                ];
                renderer.present(
                    &pixels,
                    2,
                    2,
                    8,
                    4,
                    0x00ff_0000,
                    0x0000_ff00,
                    0x0000_00ff,
                    0,
                    0,
                    2,
                    2,
                    64,
                    64,
                    false,
                )?;

                let unscaled_pixels = vec![0x7f_u8; 64 * 64 * 4];
                renderer.present(
                    &unscaled_pixels,
                    64,
                    64,
                    64 * 4,
                    4,
                    0x00ff_0000,
                    0x0000_ff00,
                    0x0000_00ff,
                    0,
                    0,
                    64,
                    64,
                    64,
                    64,
                    false,
                )
            });
            DestroyWindow(window);
            result.unwrap();
        }
    }

    #[test]
    fn expands_rgb565_components() {
        let pixel = 0b11111_100000_00000_u32;
        assert_eq!(ComponentDecoder::new(0xf800).decode(pixel), 255);
        assert!((ComponentDecoder::new(0x07e0).decode(pixel) as i16 - 130).abs() <= 1);
        assert_eq!(ComponentDecoder::new(0x001f).decode(pixel), 0);
    }

    #[test]
    fn scales_and_converts_bgrx_to_bgra() {
        let source = [
            30_u8, 20, 10, 0, 60, 50, 40, 0, 90, 80, 70, 0, 120, 110, 100, 0,
        ];
        let mut destination = [0_u8; 4];
        write_scaled_pixels(
            &mut destination,
            1,
            1,
            vk::Format::B8G8R8A8_UNORM,
            &source,
            2,
            2,
            8,
            4,
            0x00ff0000,
            0x0000ff00,
            0x000000ff,
            0,
            0,
            2,
            2,
            &mut Vec::new(),
        )
        .unwrap();
        assert_eq!(destination, [30, 20, 10, 255]);
    }

    #[test]
    fn copies_unscaled_bgrx_subrectangle_without_changing_rgb() {
        let source = [
            3_u8, 2, 1, 0, 30, 20, 10, 0, 60, 50, 40, 0, 6, 5, 4, 0, 90, 80, 70, 0, 120, 110, 100,
            0,
        ];
        let mut destination = [0_u8; 16];
        write_scaled_pixels(
            &mut destination,
            2,
            2,
            vk::Format::B8G8R8A8_UNORM,
            &source,
            3,
            2,
            12,
            4,
            0x00ff0000,
            0x0000ff00,
            0x000000ff,
            1,
            0,
            2,
            2,
            &mut Vec::new(),
        )
        .unwrap();
        assert_eq!(
            destination,
            [30, 20, 10, 255, 60, 50, 40, 255, 90, 80, 70, 255, 120, 110, 100, 255]
        );
    }

    #[test]
    fn packs_bgrx_subrectangle_for_gpu_upload() {
        let source = [
            3_u8, 2, 1, 7, 30, 20, 10, 8, 60, 50, 40, 9, 0xaa, 0xbb, 0xcc, 0xdd, 6, 5, 4, 10, 90,
            80, 70, 11, 120, 110, 100, 12, 0xee, 0xff, 0x11, 0x22,
        ];
        let mut destination = [0_u8; 16];
        copy_bgrx_source_rect(&mut destination, &source, 3, 2, 16, 1, 0, 2, 2).unwrap();
        assert_eq!(
            destination,
            [30, 20, 10, 8, 60, 50, 40, 9, 90, 80, 70, 11, 120, 110, 100, 12]
        );
    }

    #[test]
    fn fills_pixels_outside_the_desktop_surface_white() {
        let source = [3_u8, 2, 1, 0];
        let mut destination = [0_u8; 4];
        write_scaled_pixels(
            &mut destination,
            1,
            1,
            vk::Format::R8G8B8A8_UNORM,
            &source,
            1,
            1,
            4,
            4,
            0x00ff0000,
            0x0000ff00,
            0x000000ff,
            -1,
            -1,
            1,
            1,
            &mut Vec::new(),
        )
        .unwrap();
        assert_eq!(destination, [255, 255, 255, 255]);
    }
}
