use super::*;
use std::collections::HashMap;
use std::time::Instant;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct SceneCommand {
    pub rect: [i32; 4],
    pub clip: [i32; 4],
    pub data: [u32; 4],
    pub tint: [u32; 4],
    pub line: [i32; 4],
    pub extra: [u32; 4],
}

#[repr(C)]
struct Params {
    source: [f32; 4],
    target: [u32; 4],
}

#[derive(Default)]
struct SceneFrame {
    commands: StagingBuffer,
    bins: StagingBuffer,
    upload: StagingBuffer,
    output: StagingBuffer,
}

pub(super) struct SceneGpu {
    assets: Vec<u32>,
    keys: HashMap<u64, (u32, usize)>,
    uploaded: usize,
    gpu_assets: StagingBuffer,
    frames: Vec<SceneFrame>,
    layout: vk::DescriptorSetLayout,
    pool: vk::DescriptorPool,
    sets: Vec<vk::DescriptorSet>,
    pipeline_layout: vk::PipelineLayout,
    pipeline: vk::Pipeline,
    frame_count: u64,
    last_commands: usize,
    last_ms: [f64; 3],
    timestamp_pool: vk::QueryPool,
    timestamp_pending: [bool; MAX_FRAMES_IN_FLIGHT],
    timestamp_bits: u32,
    timestamp_period: f64,
    last_gpu_ms: f64,
    max_asset_words: usize,
}

impl SceneGpu {
    unsafe fn new(r: &VulkanRenderer) -> Result<Self, String> {
        let mut s = Self {
            assets: vec![0],
            keys: HashMap::new(),
            uploaded: 0,
            gpu_assets: StagingBuffer::default(),
            frames: (0..MAX_FRAMES_IN_FLIGHT)
                .map(|_| SceneFrame::default())
                .collect(),
            layout: vk::DescriptorSetLayout::null(),
            pool: vk::DescriptorPool::null(),
            sets: Vec::new(),
            pipeline_layout: vk::PipelineLayout::null(),
            pipeline: vk::Pipeline::null(),
            frame_count: 0,
            last_commands: 0,
            last_ms: [0.0; 3],
            timestamp_pool: vk::QueryPool::null(),
            timestamp_pending: [false; MAX_FRAMES_IN_FLIGHT],
            timestamp_bits: 0,
            timestamp_period: 0.0,
            last_gpu_ms: 0.0,
            max_asset_words: 0,
        };
        let result = (|| {
            let props = unsafe { r.instance.get_physical_device_properties(r.physical_device) };
            s.max_asset_words =
                (props.limits.max_storage_buffer_range as usize / 4).min(128 * 1024 * 1024);
            s.timestamp_period = props.limits.timestamp_period as f64;
            let queues = unsafe {
                r.instance
                    .get_physical_device_queue_family_properties(r.physical_device)
            };
            s.timestamp_bits = queues[r.queue_family as usize].timestamp_valid_bits;
            if s.timestamp_bits != 0 {
                s.timestamp_pool = unsafe {
                    r.device.create_query_pool(
                        &vk::QueryPoolCreateInfo::default()
                            .query_type(vk::QueryType::TIMESTAMP)
                            .query_count(2 * MAX_FRAMES_IN_FLIGHT as u32),
                        None,
                    )
                }
                .map_err(|e| format!("scene timestamp pool: {e:?}"))?;
            }
            let bindings: Vec<_> = (0..4)
                .map(|binding| {
                    vk::DescriptorSetLayoutBinding::default()
                        .binding(binding)
                        .descriptor_type(vk::DescriptorType::STORAGE_BUFFER)
                        .descriptor_count(1)
                        .stage_flags(vk::ShaderStageFlags::COMPUTE)
                })
                .collect();
            s.layout = unsafe {
                r.device.create_descriptor_set_layout(
                    &vk::DescriptorSetLayoutCreateInfo::default().bindings(&bindings),
                    None,
                )
            }
            .map_err(|e| format!("scene descriptor layout: {e:?}"))?;
            let sizes = [vk::DescriptorPoolSize {
                ty: vk::DescriptorType::STORAGE_BUFFER,
                descriptor_count: 4 * MAX_FRAMES_IN_FLIGHT as u32,
            }];
            s.pool = unsafe {
                r.device.create_descriptor_pool(
                    &vk::DescriptorPoolCreateInfo::default()
                        .max_sets(MAX_FRAMES_IN_FLIGHT as u32)
                        .pool_sizes(&sizes),
                    None,
                )
            }
            .map_err(|e| format!("scene descriptor pool: {e:?}"))?;
            let layouts = vec![s.layout; MAX_FRAMES_IN_FLIGHT];
            s.sets = unsafe {
                r.device.allocate_descriptor_sets(
                    &vk::DescriptorSetAllocateInfo::default()
                        .descriptor_pool(s.pool)
                        .set_layouts(&layouts),
                )
            }
            .map_err(|e| format!("scene sets: {e:?}"))?;
            let ranges = [vk::PushConstantRange::default()
                .stage_flags(vk::ShaderStageFlags::COMPUTE)
                .size(32)];
            s.pipeline_layout = unsafe {
                r.device.create_pipeline_layout(
                    &vk::PipelineLayoutCreateInfo::default()
                        .set_layouts(&[s.layout])
                        .push_constant_ranges(&ranges),
                    None,
                )
            }
            .map_err(|e| format!("scene pipeline layout: {e:?}"))?;
            let bytes = include_bytes!(concat!(env!("OUT_DIR"), "/scene.spv"));
            let words: Vec<u32> = bytes
                .chunks_exact(4)
                .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
                .collect();
            let shader = unsafe {
                r.device
                    .create_shader_module(&vk::ShaderModuleCreateInfo::default().code(&words), None)
            }
            .map_err(|e| format!("scene shader: {e:?}"))?;
            let info = vk::ComputePipelineCreateInfo::default()
                .layout(s.pipeline_layout)
                .stage(
                    vk::PipelineShaderStageCreateInfo::default()
                        .stage(vk::ShaderStageFlags::COMPUTE)
                        .module(shader)
                        .name(c"main"),
                );
            let pipeline = unsafe {
                r.device
                    .create_compute_pipelines(vk::PipelineCache::null(), &[info], None)
            };
            unsafe { r.device.destroy_shader_module(shader, None) };
            s.pipeline = pipeline.map_err(|(_, e)| format!("scene compute pipeline: {e:?}"))?[0];
            Ok(())
        })();
        if let Err(error) = result {
            unsafe { s.destroy(r) };
            return Err(error);
        }
        Ok(s)
    }

    pub(super) unsafe fn destroy(&mut self, r: &VulkanRenderer) {
        unsafe {
            for f in &mut self.frames {
                r.free_staging_buffer(&mut f.commands);
                r.free_staging_buffer(&mut f.bins);
                r.free_staging_buffer(&mut f.upload);
                r.free_staging_buffer(&mut f.output);
            }
            r.free_staging_buffer(&mut self.gpu_assets);
            r.device.destroy_query_pool(self.timestamp_pool, None);
            r.device.destroy_pipeline(self.pipeline, None);
            r.device.destroy_pipeline_layout(self.pipeline_layout, None);
            r.device.destroy_descriptor_pool(self.pool, None);
            r.device.destroy_descriptor_set_layout(self.layout, None);
        }
    }
}

// Same ownership helper for mapped command buffers and device-local assets/output.
unsafe fn device_buffer(r: &VulkanRenderer, len: usize) -> Result<StagingBuffer, String> {
    let info = vk::BufferCreateInfo::default().size(len as u64).usage(
        vk::BufferUsageFlags::STORAGE_BUFFER
            | vk::BufferUsageFlags::TRANSFER_DST
            | vk::BufferUsageFlags::TRANSFER_SRC,
    );
    let buffer = unsafe { r.device.create_buffer(&info, None) }
        .map_err(|e| format!("scene buffer: {e:?}"))?;
    let requirements = unsafe { r.device.get_buffer_memory_requirements(buffer) };
    let props = unsafe {
        r.instance
            .get_physical_device_memory_properties(r.physical_device)
    };
    let memory_type = (0..props.memory_type_count).find(|i| {
        requirements.memory_type_bits & (1 << i) != 0
            && props.memory_types[*i as usize]
                .property_flags
                .contains(vk::MemoryPropertyFlags::DEVICE_LOCAL)
    });
    let Some(memory_type) = memory_type else {
        unsafe { r.device.destroy_buffer(buffer, None) };
        return Err("no device-local scene memory".into());
    };
    let memory = match unsafe {
        r.device.allocate_memory(
            &vk::MemoryAllocateInfo::default()
                .allocation_size(requirements.size)
                .memory_type_index(memory_type),
            None,
        )
    } {
        Ok(memory) => memory,
        Err(e) => {
            unsafe { r.device.destroy_buffer(buffer, None) };
            return Err(format!("scene memory: {e:?}"));
        }
    };
    if let Err(e) = unsafe { r.device.bind_buffer_memory(buffer, memory, 0) } {
        unsafe {
            r.device.destroy_buffer(buffer, None);
            r.device.free_memory(memory, None);
        }
        return Err(format!("scene bind memory: {e:?}"));
    }
    Ok(StagingBuffer {
        buffer,
        memory,
        mapped: ptr::null_mut(),
        len,
    })
}

unsafe fn reserve(
    r: &VulkanRenderer,
    buffer: &mut StagingBuffer,
    len: usize,
    local: bool,
) -> Result<(), String> {
    if buffer.len >= len {
        return Ok(());
    }
    let len = len
        .max(256)
        .checked_next_power_of_two()
        .ok_or("scene allocation overflow")?;
    let limit = unsafe { r.instance.get_physical_device_properties(r.physical_device) }
        .limits
        .max_storage_buffer_range as usize;
    if len > limit {
        return Err("scene buffer exceeds the device storage-buffer limit".into());
    }
    let new = if local {
        unsafe { device_buffer(r, len) }?
    } else {
        unsafe { r.create_staging_buffer(len) }?
    };
    unsafe { r.free_staging_buffer(buffer) };
    *buffer = new;
    Ok(())
}

fn make_bins(
    commands: &[SceneCommand],
    source: [f32; 4],
    width: u32,
    height: u32,
) -> Result<Vec<u32>, String> {
    let nx = width.div_ceil(32) as usize;
    let ny = height.div_ceil(32) as usize;
    let mut bins = vec![Vec::new(); nx * ny];
    let mut references = 0usize;
    for (index, c) in commands.iter().enumerate() {
        let source = if c.extra[2] != 0 {
            [source[0], source[1], 1.0, 1.0]
        } else {
            source
        };
        let left = c.rect[0].max(c.clip[0]);
        let top = c.rect[1].max(c.clip[1]);
        let right = c.rect[0].saturating_add(c.rect[2]).min(c.clip[2]);
        let bottom = c.rect[1].saturating_add(c.rect[3]).min(c.clip[3]);
        if left >= right || top >= bottom {
            continue;
        }
        let x0 = (((left as f32 - source[0]) / source[2]).floor() as i32).clamp(0, width as i32);
        let y0 = (((top as f32 - source[1]) / source[3]).floor() as i32).clamp(0, height as i32);
        let x1 = (((right as f32 - source[0]) / source[2]).ceil() as i32).clamp(0, width as i32);
        let y1 = (((bottom as f32 - source[1]) / source[3]).ceil() as i32).clamp(0, height as i32);
        if x0 >= x1 || y0 >= y1 {
            continue;
        }
        references = references.saturating_add(
            ((x1 as usize - 1) / 32 - x0 as usize / 32 + 1)
                * ((y1 as usize - 1) / 32 - y0 as usize / 32 + 1),
        );
        if references > 16 * 1024 * 1024 {
            return Err("scene bin list exceeds 64 MiB".into());
        }
        for y in y0 as usize / 32..=(y1 as usize - 1) / 32 {
            for x in x0 as usize / 32..=(x1 as usize - 1) / 32 {
                bins[y * nx + x].push(index as u32);
            }
        }
    }
    let mut result = vec![0; bins.len() * 2];
    for (i, bin) in bins.into_iter().enumerate() {
        result[i * 2] = result.len() as u32;
        result[i * 2 + 1] = bin.len() as u32;
        result.extend(bin);
    }
    Ok(result)
}

unsafe fn bytes<T>(items: &[T]) -> &[u8] {
    unsafe { slice::from_raw_parts(items.as_ptr().cast(), std::mem::size_of_val(items)) }
}

impl VulkanRenderer {
    unsafe fn present_scene(
        &mut self,
        s: &mut SceneGpu,
        commands: &[SceneCommand],
        source: [f32; 4],
        width: u32,
        height: u32,
        vsync: bool,
    ) -> Result<(), String> {
        let start = Instant::now();
        let requested = vk::Extent2D { width, height };
        if self.swapchain_dirty
            || self.swapchain == vk::SwapchainKHR::null()
            || self.requested_extent != requested
            || self.vsync != vsync
        {
            unsafe { self.recreate_swapchain(width, height, vsync) }?;
        }
        let frame = self.current_frame;
        let fence = self.frames[frame].fence;
        let available = self.frames[frame].image_available;
        let cb = self.frames[frame].command_buffer;
        unsafe { self.device.wait_for_fences(&[fence], true, u64::MAX) }
            .map_err(|e| format!("scene frame wait: {e:?}"))?;
        if s.timestamp_pending[frame] {
            let mut stamps = [0u64; 2];
            if unsafe {
                self.device.get_query_pool_results(
                    s.timestamp_pool,
                    frame as u32 * 2,
                    &mut stamps,
                    vk::QueryResultFlags::TYPE_64,
                )
            }
            .is_ok()
            {
                let mask = u64::MAX >> (64 - s.timestamp_bits);
                s.last_gpu_ms = (stamps[1].wrapping_sub(stamps[0]) & mask) as f64
                    * s.timestamp_period
                    / 1_000_000.0;
            }
        }
        let (index, suboptimal) = match unsafe {
            self.swapchain_loader.acquire_next_image(
                self.swapchain,
                u64::MAX,
                available,
                vk::Fence::null(),
            )
        } {
            Ok(value) => value,
            Err(vk::Result::ERROR_OUT_OF_DATE_KHR) => {
                self.swapchain_dirty = true;
                return Ok(());
            }
            Err(e) => return Err(format!("scene acquire: {e:?}")),
        };
        let acquired = Instant::now();
        let extent = self.swapchain_extent;
        let mut source = source;
        source[2] *= width as f32 / extent.width as f32;
        source[3] *= height as f32 / extent.height as f32;
        let bins = make_bins(commands, source, extent.width, extent.height)?;
        if s.gpu_assets.len < s.assets.len() * 4 {
            unsafe { self.device.device_wait_idle() }
                .map_err(|e| format!("scene asset growth wait: {e:?}"))?;
            unsafe { reserve(self, &mut s.gpu_assets, s.assets.len() * 4, true) }?;
            s.uploaded = 0;
        }
        let f = &mut s.frames[frame];
        unsafe {
            reserve(
                self,
                &mut f.commands,
                std::mem::size_of_val(commands).max(96),
                false,
            )?;
            reserve(self, &mut f.bins, bins.len() * 4, false)?;
            reserve(
                self,
                &mut f.output,
                extent.width as usize * extent.height as usize * 4,
                true,
            )?;
            ptr::copy_nonoverlapping(
                commands.as_ptr().cast::<u8>(),
                f.commands.mapped,
                std::mem::size_of_val(commands),
            );
            ptr::copy_nonoverlapping(bins.as_ptr().cast::<u8>(), f.bins.mapped, bins.len() * 4);
        }
        let pending = &s.assets[s.uploaded..];
        if !pending.is_empty() {
            unsafe {
                reserve(self, &mut f.upload, pending.len() * 4, false)?;
                ptr::copy_nonoverlapping(
                    pending.as_ptr().cast::<u8>(),
                    f.upload.mapped,
                    pending.len() * 4,
                );
            }
        }
        let buffers = [&s.gpu_assets, &f.commands, &f.bins, &f.output];
        let infos: Vec<_> = buffers
            .iter()
            .map(|b| {
                [vk::DescriptorBufferInfo::default()
                    .buffer(b.buffer)
                    .range(b.len as u64)]
            })
            .collect();
        let writes: Vec<_> = infos
            .iter()
            .enumerate()
            .map(|(binding, info)| {
                vk::WriteDescriptorSet::default()
                    .dst_set(s.sets[frame])
                    .dst_binding(binding as u32)
                    .descriptor_type(vk::DescriptorType::STORAGE_BUFFER)
                    .buffer_info(info)
            })
            .collect();
        unsafe {
            self.device.update_descriptor_sets(&writes, &[]);
            self.device
                .reset_command_buffer(cb, vk::CommandBufferResetFlags::empty())
                .map_err(|e| format!("scene reset: {e:?}"))?;
            self.device
                .begin_command_buffer(
                    cb,
                    &vk::CommandBufferBeginInfo::default()
                        .flags(vk::CommandBufferUsageFlags::ONE_TIME_SUBMIT),
                )
                .map_err(|e| format!("scene begin: {e:?}"))?;
            if s.timestamp_bits != 0 {
                self.device
                    .cmd_reset_query_pool(cb, s.timestamp_pool, frame as u32 * 2, 2);
                self.device.cmd_write_timestamp(
                    cb,
                    vk::PipelineStageFlags::TOP_OF_PIPE,
                    s.timestamp_pool,
                    frame as u32 * 2,
                );
            }
            if !pending.is_empty() {
                self.device.cmd_copy_buffer(
                    cb,
                    f.upload.buffer,
                    s.gpu_assets.buffer,
                    &[vk::BufferCopy {
                        src_offset: 0,
                        dst_offset: s.uploaded as u64 * 4,
                        size: pending.len() as u64 * 4,
                    }],
                );
            }
            self.device.cmd_pipeline_barrier(
                cb,
                vk::PipelineStageFlags::TRANSFER | vk::PipelineStageFlags::HOST,
                vk::PipelineStageFlags::COMPUTE_SHADER,
                vk::DependencyFlags::empty(),
                &[vk::MemoryBarrier::default()
                    .src_access_mask(vk::AccessFlags::TRANSFER_WRITE | vk::AccessFlags::HOST_WRITE)
                    .dst_access_mask(vk::AccessFlags::SHADER_READ)],
                &[],
                &[],
            );
            self.device
                .cmd_bind_pipeline(cb, vk::PipelineBindPoint::COMPUTE, s.pipeline);
            self.device.cmd_bind_descriptor_sets(
                cb,
                vk::PipelineBindPoint::COMPUTE,
                s.pipeline_layout,
                0,
                &[s.sets[frame]],
                &[],
            );
            let params = Params {
                source,
                target: [
                    extent.width,
                    extent.height,
                    extent.width.div_ceil(32),
                    u32::from(matches!(
                        self.swapchain_format,
                        vk::Format::R8G8B8A8_UNORM | vk::Format::R8G8B8A8_SRGB
                    )),
                ],
            };
            self.device.cmd_push_constants(
                cb,
                s.pipeline_layout,
                vk::ShaderStageFlags::COMPUTE,
                0,
                bytes(&[params]),
            );
            self.device
                .cmd_dispatch(cb, extent.width.div_ceil(8), extent.height.div_ceil(8), 1);
            let range = vk::ImageSubresourceRange::default()
                .aspect_mask(vk::ImageAspectFlags::COLOR)
                .level_count(1)
                .layer_count(1);
            let image_barrier = vk::ImageMemoryBarrier::default()
                .image(self.swapchain_images[index as usize])
                .subresource_range(range)
                .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
                .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
                .old_layout(vk::ImageLayout::UNDEFINED)
                .new_layout(vk::ImageLayout::TRANSFER_DST_OPTIMAL)
                .dst_access_mask(vk::AccessFlags::TRANSFER_WRITE);
            self.device.cmd_pipeline_barrier(
                cb,
                vk::PipelineStageFlags::COMPUTE_SHADER,
                vk::PipelineStageFlags::TRANSFER,
                vk::DependencyFlags::empty(),
                &[vk::MemoryBarrier::default()
                    .src_access_mask(vk::AccessFlags::SHADER_WRITE)
                    .dst_access_mask(vk::AccessFlags::TRANSFER_READ)],
                &[],
                &[image_barrier],
            );
            let copy = vk::BufferImageCopy::default()
                .image_subresource(
                    vk::ImageSubresourceLayers::default()
                        .aspect_mask(vk::ImageAspectFlags::COLOR)
                        .layer_count(1),
                )
                .image_extent(vk::Extent3D {
                    width: extent.width,
                    height: extent.height,
                    depth: 1,
                });
            self.device.cmd_copy_buffer_to_image(
                cb,
                f.output.buffer,
                self.swapchain_images[index as usize],
                vk::ImageLayout::TRANSFER_DST_OPTIMAL,
                &[copy],
            );
            let present_barrier = image_barrier
                .old_layout(vk::ImageLayout::TRANSFER_DST_OPTIMAL)
                .new_layout(vk::ImageLayout::PRESENT_SRC_KHR)
                .src_access_mask(vk::AccessFlags::TRANSFER_WRITE)
                .dst_access_mask(vk::AccessFlags::empty());
            self.device.cmd_pipeline_barrier(
                cb,
                vk::PipelineStageFlags::TRANSFER,
                vk::PipelineStageFlags::BOTTOM_OF_PIPE,
                vk::DependencyFlags::empty(),
                &[],
                &[],
                &[present_barrier],
            );
            if s.timestamp_bits != 0 {
                self.device.cmd_write_timestamp(
                    cb,
                    vk::PipelineStageFlags::BOTTOM_OF_PIPE,
                    s.timestamp_pool,
                    frame as u32 * 2 + 1,
                );
            }
            self.device
                .end_command_buffer(cb)
                .map_err(|e| format!("scene end: {e:?}"))?;
        }
        let recorded = Instant::now();
        let complete = [self.present_semaphores[index as usize]];
        let available = [available];
        let stages = [vk::PipelineStageFlags::TRANSFER];
        let buffers = [cb];
        let submit = vk::SubmitInfo::default()
            .wait_semaphores(&available)
            .wait_dst_stage_mask(&stages)
            .command_buffers(&buffers)
            .signal_semaphores(&complete);
        unsafe { self.device.reset_fences(&[fence]) }
            .map_err(|e| format!("scene reset fence: {e:?}"))?;
        if let Err(e) = unsafe { self.device.queue_submit(self.queue, &[submit], fence) } {
            unsafe { self.recover_signaled_fence(frame) };
            return Err(format!("scene submit: {e:?}"));
        }
        s.uploaded = s.assets.len();
        s.timestamp_pending[frame] = s.timestamp_bits != 0;
        self.current_frame = (frame + 1) % self.frames.len();
        self.image_initialized[index as usize] = true;
        let swapchains = [self.swapchain];
        let indices = [index];
        let info = vk::PresentInfoKHR::default()
            .swapchains(&swapchains)
            .image_indices(&indices)
            .wait_semaphores(&complete);
        match unsafe { self.swapchain_loader.queue_present(self.queue, &info) } {
            Ok(changed) => self.swapchain_dirty = suboptimal || changed,
            Err(vk::Result::ERROR_OUT_OF_DATE_KHR) => self.swapchain_dirty = true,
            Err(e) => return Err(format!("scene present: {e:?}")),
        }
        s.frame_count += 1;
        s.last_commands = commands.len();
        s.last_ms = [
            acquired.duration_since(start).as_secs_f64() * 1000.0,
            recorded.duration_since(acquired).as_secs_f64() * 1000.0,
            recorded.elapsed().as_secs_f64() * 1000.0,
        ];
        Ok(())
    }
}

#[no_mangle]
pub unsafe extern "C" fn rs_vulkan_scene_upload(
    renderer: *mut RsVulkanRenderer,
    key: u64,
    words: *const u32,
    count: usize,
    offset: *mut u32,
) -> i32 {
    if renderer.is_null()
        || words.is_null()
        || offset.is_null()
        || count == 0
        || count > 128 * 1024 * 1024
    {
        return RS_ERR_BAD_ARG;
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let r = unsafe { &mut (*renderer).inner };
        if r.scene.is_none() {
            r.scene = Some(unsafe { SceneGpu::new(r) }?);
        }
        let s = r.scene.as_mut().unwrap();
        if let Some(&(existing, size)) = s.keys.get(&key) {
            if size != count {
                return Err("scene asset key reused with different size".into());
            }
            unsafe { *offset = existing };
            return Ok(());
        }
        if s.assets.len().saturating_add(count) > s.max_asset_words {
            return Err("scene asset cache exceeds the device storage-buffer limit; reload map to release assets".into());
        }
        let start = s.assets.len() as u32;
        s.assets
            .extend_from_slice(unsafe { slice::from_raw_parts(words, count) });
        s.keys.insert(key, (start, count));
        unsafe { *offset = start };
        Ok(())
    }));
    ffi_result(result)
}

fn ffi_result(result: Result<Result<(), String>, Box<dyn std::any::Any + Send>>) -> i32 {
    match result {
        Ok(Ok(())) => RS_OK,
        Ok(Err(e)) => {
            set_last_error(e);
            RS_ERR_VULKAN_RUNTIME
        }
        Err(_) => {
            set_last_error("panic in Vulkan scene renderer");
            RS_ERR_PANIC
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn rs_vulkan_scene_present(
    renderer: *mut RsVulkanRenderer,
    commands: *const SceneCommand,
    count: usize,
    left: f32,
    top: f32,
    scale_x: f32,
    scale_y: f32,
    width: u32,
    height: u32,
    vsync: i32,
) -> i32 {
    if renderer.is_null()
        || commands.is_null()
        || count > 1_000_000
        || width == 0
        || height == 0
        || width > 16384
        || height > 16384
        || ![left, top, scale_x, scale_y].iter().all(|f| f.is_finite())
        || scale_x <= 0.0
        || scale_y <= 0.0
    {
        return RS_ERR_BAD_ARG;
    }
    ffi_result(catch_unwind(AssertUnwindSafe(|| {
        let r = unsafe { &mut (*renderer).inner };
        let mut scene = match r.scene.take() {
            Some(scene) => scene,
            None => unsafe { SceneGpu::new(r) }?,
        };
        let commands = unsafe { slice::from_raw_parts(commands, count) };
        let result = validate_commands(commands, scene.assets.len()).and_then(|_| unsafe {
            r.present_scene(
                &mut scene,
                commands,
                [left, top, scale_x, scale_y],
                width,
                height,
                vsync != 0,
            )
        });
        r.scene = Some(scene);
        result
    })))
}

fn validate_commands(commands: &[SceneCommand], asset_len: usize) -> Result<(), String> {
    for c in commands {
        if c.data[0] > 2
            || c.rect[2] < 0
            || c.rect[3] < 0
            || c.rect
                .iter()
                .chain(c.clip.iter())
                .chain(c.line.iter())
                .any(|v| !(-65536..=65536).contains(v))
        {
            return Err("invalid scene command geometry".into());
        }
        if c.data[0] == 2 {
            let dx = (i64::from(c.line[2]) - i64::from(c.line[0])).abs();
            let dy = (i64::from(c.line[3]) - i64::from(c.line[1])).abs();
            if 2 * dx * dy + dx.max(dy) > i64::from(i32::MAX) {
                return Err("scene line arithmetic exceeds shader range".into());
            }
            continue;
        }
        let pixels = (c.data[2] as usize)
            .checked_mul(c.data[3] as usize)
            .ok_or("scene texture size overflow")?;
        if (c.data[1] as usize)
            .checked_add(pixels)
            .is_none_or(|end| end > asset_len)
            || c.extra[0]
                .checked_add(c.rect[2] as u32)
                .is_none_or(|end| end > c.data[2])
            || c.extra[1]
                .checked_add(c.rect[3] as u32)
                .is_none_or(|end| end > c.data[3])
            || (c.data[0] == 0
                && (c.tint[0] as usize)
                    .checked_add(256)
                    .is_none_or(|end| end > asset_len))
        {
            return Err("scene command references pixels outside asset storage".into());
        }
    }
    Ok(())
}

#[no_mangle]
pub unsafe extern "C" fn rs_vulkan_scene_reset(renderer: *mut RsVulkanRenderer) -> i32 {
    if renderer.is_null() {
        return RS_ERR_BAD_ARG;
    }
    ffi_result(catch_unwind(AssertUnwindSafe(|| {
        let r = unsafe { &mut (*renderer).inner };
        unsafe { r.device.device_wait_idle() }.map_err(|e| format!("scene reset wait: {e:?}"))?;
        if let Some(mut scene) = r.scene.take() {
            unsafe { scene.destroy(r) };
        }
        Ok(())
    })))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[link(name = "user32")]
    unsafe extern "system" {
        fn CreateWindowExW(
            ex: u32,
            class: *const u16,
            name: *const u16,
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
    struct Window(*mut c_void);
    impl Drop for Window {
        fn drop(&mut self) {
            unsafe {
                DestroyWindow(self.0);
            }
        }
    }
    unsafe fn window() -> Window {
        let name: Vec<u16> = "STATIC\0".encode_utf16().collect();
        let handle = unsafe {
            CreateWindowExW(
                0,
                name.as_ptr(),
                name.as_ptr(),
                0,
                0,
                0,
                64,
                64,
                ptr::null_mut(),
                ptr::null_mut(),
                GetModuleHandleW(ptr::null()),
                ptr::null_mut(),
            )
        };
        assert!(!handle.is_null());
        Window(handle)
    }
    unsafe fn read_output(r: &VulkanRenderer, s: &SceneGpu) -> Vec<u32> {
        let frame = (r.current_frame + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;
        unsafe { r.device.device_wait_idle() }.unwrap();
        let len = r.swapchain_extent.width as usize * r.swapchain_extent.height as usize;
        let mut download = unsafe { r.create_staging_buffer(len * 4) }.unwrap();
        let cb = r.frames[frame].command_buffer;
        unsafe {
            r.device
                .reset_command_buffer(cb, vk::CommandBufferResetFlags::empty())
                .unwrap();
            r.device
                .begin_command_buffer(cb, &vk::CommandBufferBeginInfo::default())
                .unwrap();
            r.device.cmd_pipeline_barrier(
                cb,
                vk::PipelineStageFlags::COMPUTE_SHADER,
                vk::PipelineStageFlags::TRANSFER,
                vk::DependencyFlags::empty(),
                &[vk::MemoryBarrier::default()
                    .src_access_mask(vk::AccessFlags::SHADER_WRITE)
                    .dst_access_mask(vk::AccessFlags::TRANSFER_READ)],
                &[],
                &[],
            );
            r.device.cmd_copy_buffer(
                cb,
                s.frames[frame].output.buffer,
                download.buffer,
                &[vk::BufferCopy {
                    src_offset: 0,
                    dst_offset: 0,
                    size: (len * 4) as u64,
                }],
            );
            r.device.cmd_pipeline_barrier(
                cb,
                vk::PipelineStageFlags::TRANSFER,
                vk::PipelineStageFlags::HOST,
                vk::DependencyFlags::empty(),
                &[vk::MemoryBarrier::default()
                    .src_access_mask(vk::AccessFlags::TRANSFER_WRITE)
                    .dst_access_mask(vk::AccessFlags::HOST_READ)],
                &[],
                &[],
            );
            r.device.end_command_buffer(cb).unwrap();
            r.device
                .queue_submit(
                    r.queue,
                    &[vk::SubmitInfo::default().command_buffers(&[cb])],
                    vk::Fence::null(),
                )
                .unwrap();
            r.device.queue_wait_idle(r.queue).unwrap();
        }
        let pixels = unsafe { slice::from_raw_parts(download.mapped.cast::<u32>(), len) }.to_vec();
        unsafe { r.free_staging_buffer(&mut download) };
        pixels
    }

    #[test]
    fn gpu_scene_matches_palette_remap_lighting_dither_and_clipping() {
        unsafe {
            let window = window();
            let mut r = VulkanRenderer::new(window.0).unwrap();
            let mut s = SceneGpu::new(&r).unwrap();
            let mut colors = vec![0; 256];
            colors[1] = 0x000000ff;
            colors[16] = 0x00ff0000;
            s.assets = colors;
            s.assets.extend(
                (0..256)
                    .map(|i| 16 | (47 << 8) | if (i % 16 + i / 16) % 2 == 0 { 65536 } else { 0 }),
            );
            let mut a = SceneCommand::default();
            a.rect = [8, 8, 16, 16];
            a.clip = [10, 10, 22, 22];
            a.data = [0, 256, 16, 16];
            a.tint = [0, 0x00336699, 1 | 2 | 4, 0];
            r.present_scene(&mut s, &[a], [0.0, 0.0, 1.0, 1.0], 64, 64, true)
                .unwrap();
            let image = read_output(&r, &s);
            let width = r.swapchain_extent.width as usize;
            let height = r.swapchain_extent.height as usize;
            for y in 0..height {
                for x in 0..width {
                    let px = (x as f32 * 64.0 / width as f32).floor() as usize;
                    let py = (y as f32 * 64.0 / height as f32).floor() as usize;
                    let visible =
                        (10..22).contains(&px) && (10..22).contains(&py) && (px + py) % 2 == 0;
                    assert_eq!(
                        image[y * width + x],
                        if visible { 0xff336699 } else { 0xffffffff },
                        "pixel {x},{y}"
                    );
                }
            }
            let bitmap_offset = s.assets.len() as u32;
            s.assets.extend([0xffff00ff, 0xff000000, 0, 0xff00ff00]);
            let mut bitmap = SceneCommand::default();
            bitmap.rect = [10, 10, 2, 2];
            bitmap.clip = [0, 0, 64, 64];
            bitmap.data = [1, bitmap_offset, 2, 2];
            bitmap.extra[2] = 1; // text/icon layer stays in screen pixels
            let mut line = SceneCommand::default();
            line.rect = [30, 5, 1, 16];
            line.clip = [0, 0, 64, 64];
            line.data[0] = 2;
            line.line = [30, 5, 30, 20];
            line.tint[1] = 0x00808000;
            line.extra[0] = 3;
            let light_offset = s.assets.len() as u32;
            s.assets.push(20 | (200 << 8));
            let mut lit = SceneCommand::default();
            lit.rect = [32, 16, 1, 1];
            lit.clip = [0, 0, 64, 64];
            lit.data = [0, light_offset, 1, 1];
            lit.tint = [0, 0x00336699, 2 | 4, 0];
            r.present_scene(
                &mut s,
                &[a, bitmap, line, lit],
                [0.0, 0.0, 1.0, 1.0],
                width as u32,
                height as u32,
                true,
            )
            .unwrap();
            let image = read_output(&r, &s);
            assert_eq!(image[10 * width + 10], 0xffff00ff);
            assert_eq!(image[10 * width + 11], 0xff000000);
            assert_eq!(image[11 * width + 10], 0xffffffff); // transparent texel retains background
            assert_eq!(image[11 * width + 11], 0xff00ff00);
            assert_eq!(image[5 * width + 30], 0xff808000);
            assert_eq!(image[6 * width + 30], 0xffffffff);
            assert_eq!(image[8 * width + 30], 0xff808000);
            if height > 16 {
                assert_eq!(image[16 * width + 32], 0xff3f7ebf);
            }
            r.present_scene(
                &mut s,
                &[bitmap],
                [0.0, 0.0, 0.5, 0.5],
                width as u32,
                height as u32,
                true,
            )
            .unwrap();
            let image = read_output(&r, &s);
            assert_eq!(image[10 * width + 10], 0xffff00ff);
            // Reuse both in-flight frames many times and alternate swapchain
            // recreation with persistent assets and different source scales.
            for frame in 0..180 {
                if frame % 45 == 0 {
                    r.swapchain_dirty = true;
                }
                r.present_scene(
                    &mut s,
                    &[a],
                    [0.0, 0.0, if frame % 2 == 0 { 1.0 } else { 0.5 }, 1.0],
                    64,
                    64,
                    frame % 90 < 45,
                )
                .unwrap();
                assert_eq!(r.present_semaphores.len(), r.swapchain_images.len());
                assert!(
                    r.present_mode == vk::PresentModeKHR::FIFO
                        || r.present_mode == vk::PresentModeKHR::MAILBOX
                );
            }
            println!(
                "GPU scene stress: {} frames, last wait/record/present {:?} ms",
                s.frame_count, s.last_ms
            );
            r.device.device_wait_idle().unwrap();
            s.destroy(&r);
        }
    }

    #[test]
    fn rejects_out_of_range_gpu_asset_references() {
        let mut c = SceneCommand::default();
        c.rect = [0, 0, 20, 20];
        c.data = [0, 256, 20, 20];
        assert!(validate_commands(&[c], 656).is_ok());
        assert!(validate_commands(&[c], 655).is_err());
        c.extra[0] = 1;
        assert!(validate_commands(&[c], 656).is_err());
        c.data[1] = u32::MAX;
        assert!(validate_commands(&[c], 656).is_err());
    }

    #[test]
    fn bins_preserve_painter_order_and_clip_negative_coordinates() {
        let mut a = SceneCommand::default();
        a.rect = [-10, 0, 80, 64];
        a.clip = [0, 0, 64, 64];
        let mut b = a;
        b.rect = [32, 32, 32, 32];
        let bins = make_bins(&[a, b], [0.0, 0.0, 1.0, 1.0], 64, 64).unwrap();
        assert_eq!(bins[1], 1);
        assert_eq!(bins[7], 2);
        let offset = bins[6] as usize;
        assert_eq!(&bins[offset..offset + 2], &[0, 1]);
    }
}

#[no_mangle]
pub unsafe extern "C" fn rs_vulkan_info(
    renderer: *const RsVulkanRenderer,
    dst: *mut c_char,
    cap: usize,
) -> usize {
    if renderer.is_null() || dst.is_null() || cap == 0 {
        return 0;
    }
    catch_unwind(AssertUnwindSafe(|| {
    let r = unsafe { &(*renderer).inner };
    let props = unsafe { r.instance.get_physical_device_properties(r.physical_device) };
    let device = unsafe { CStr::from_ptr(props.device_name.as_ptr()) }.to_string_lossy();
    let mode = if r.present_mode == vk::PresentModeKHR::MAILBOX {
        "MAILBOX"
    } else {
        "FIFO"
    };
    let mut message = format!(
        "device={device}; mode={mode}; images={}; frames-in-flight={}",
        r.swapchain_images.len(),
        r.frames.len()
    );
    if let Some(s) = &r.scene {
        message.push_str(&format!("; GPU scene frames={}; commands={}; assets={} MiB; wait/record/present={:.2}/{:.2}/{:.2} ms; GPU={:.2} ms", s.frame_count, s.last_commands, s.assets.len() / 262144, s.last_ms[0], s.last_ms[1], s.last_ms[2], s.last_gpu_ms));
    }
    let n = message.len().min(cap - 1);
    unsafe {
        ptr::copy_nonoverlapping(message.as_ptr(), dst.cast(), n);
        *dst.add(n) = 0;
    }
    message.len()
    })).unwrap_or(0)
}
