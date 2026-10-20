// Included after Backend::State, inside gpu::vulkan.
namespace {
uint64_t VulkanPipelineKey(const PipelineDesc& d) {
  const uint64_t logical = PipelineKey(d.vs.key, d.ps.key, d.layout_id, d.state);
  const uint32_t spec[]{d.vs.spec_mask, d.vs.spec_value & d.vs.spec_mask,
                         d.ps.spec_mask, d.ps.spec_value & d.ps.spec_mask};
  uint64_t hash = logical;
  for (uint32_t value : spec) for (unsigned i = 0; i < 4; ++i) {
    hash ^= (value >> (8 * i)) & 255;
    hash *= 0x100000001B3ull;
  }
  return hash;
}
ShaderRef PassShader(const char* name, ShaderStage stage) {
  std::string error;
  const auto* package = EmbeddedShaderPackage(ShaderFormat::kSpirv, error);
  ShaderRef shader{name, stage, ShaderFormat::kSpirv};
  if (package) {
    const auto it = package->passes.find(name);
    if (it != package->passes.end())
      shader.bytes = std::make_shared<const std::vector<uint8_t>>(DecodeShaderModule(*package, it->second));
  }
  return shader;
}
bool GraphicsError(std::string& evidence, const char* reason, VkResult result = VK_ERROR_UNKNOWN) {
  evidence = fmt::format("{{\"reason\":\"{}\",\"vk_result\":{}}}", reason, int32_t(result));
  return false;
}
}

bool Backend::State::EnsureGraphics(std::string& evidence) {
  if (draw_layout) return true;
  VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
  VkPhysicalDeviceCustomBorderColorFeaturesEXT border{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT};
  VkPhysicalDeviceDescriptorIndexingFeatures indexing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
  dynamic.pNext = &border;
  border.pNext = &indexing;
  VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &dynamic};
  vkGetPhysicalDeviceFeatures2(vulkan->physicalDevice, &features);
  custom_border = border.customBorderColors && border.customBorderColorWithoutFormat;
  if (!dynamic.extendedDynamicState || !vkCmdBindVertexBuffers2EXT || !vkCmdSetPrimitiveTopologyEXT)
    return GraphicsError(evidence, "native-vulkan-extended-dynamic-state-required", VK_ERROR_FEATURE_NOT_PRESENT);
  // The draw heap's bindings. Plume enables every descriptor indexing feature
  // the device supports (capabilities.descriptorIndexing).
  VkPhysicalDeviceDescriptorIndexingProperties limits{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
  VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &limits};
  vkGetPhysicalDeviceProperties2(vulkan->physicalDevice, &properties);
  constexpr uint32_t kHeapImages = 2 * kHeapViewSlots + 1;
  if (!vulkan->capabilities.descriptorIndexing || !indexing.descriptorBindingPartiallyBound ||
      !indexing.descriptorBindingSampledImageUpdateAfterBind || !indexing.descriptorBindingUpdateUnusedWhilePending ||
      limits.maxPerStageDescriptorUpdateAfterBindSampledImages < kHeapImages ||
      limits.maxDescriptorSetUpdateAfterBindSampledImages < kHeapImages ||
      limits.maxPerStageDescriptorUpdateAfterBindSamplers < kHeapSamplerSlots ||
      limits.maxDescriptorSetUpdateAfterBindSamplers < kHeapSamplerSlots ||
      limits.maxPerStageUpdateAfterBindResources < kHeapImages + kHeapSamplerSlots ||
      limits.maxUpdateAfterBindDescriptorsInAllPools < kHeapImages + kHeapSamplerSlots)
    return GraphicsError(evidence, "native-vulkan-descriptor-heap-unsupported", VK_ERROR_FEATURE_NOT_PRESENT);
  // The compiler's exported semantic locations, from the same embedded manifest
  // whose capabilities were checked at Initialize. No second location table.
  std::string error;
  const auto* package = EmbeddedShaderPackage(ShaderFormat::kSpirv, error);
  std::smatch block;
  const std::regex section(R"("vertex_input_locations"\s*:\s*\{([^}]+)\})");
  if (!package || !std::regex_search(package->manifest, block, section))
    return GraphicsError(evidence, "native-vulkan-vertex-location-manifest");
  const std::string entries = block[1];
  const std::regex entry(R"re("([A-Z]+[0-9]+)"\s*:\s*\[\s*([0-9]+)\s*\])re");
  for (std::sregex_iterator it(entries.begin(), entries.end(), entry), end; it != end; ++it)
    vertex_locations[(*it)[1]] = uint32_t(std::stoul((*it)[2]));
  if (vertex_locations.empty()) return GraphicsError(evidence, "native-vulkan-vertex-locations-empty");
  if (!CreateHeap(evidence)) { ReleaseGraphics(); return false; }
  VkResult result = VK_SUCCESS;
  VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 24};
  VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  info.setLayoutCount = 4; info.pSetLayouts = draw_sets.data(); info.pushConstantRangeCount = 1; info.pPushConstantRanges = &push;
  if ((result = vkCreatePipelineLayout(vulkan->vk, &info, nullptr, &draw_layout)) != VK_SUCCESS) {
    ReleaseGraphics(); return GraphicsError(evidence, "native-vulkan-draw-layout", result);
  }
  pipeline_set.Configure([this](const PipelineDesc& d, PipelineSet::Pipeline& out) {
    return MakeGraphicsPipeline(vulkan->vk, draw_layout, d, vertex_locations, out);
  });
  return true;
}

// The draw heap (Backend::State): its set layouts, its pool and sets, and slot 0.
bool Backend::State::CreateHeap(std::string& evidence) {
  VkResult result = VK_SUCCESS;
  // Sets 0 to 2: the 2D, 3D and cube images (the 2D and cube sets share the
  // image slots; no texture of the title is 3D); set 3: the samplers.
  constexpr uint32_t counts[]{kHeapViewSlots, 1, kHeapViewSlots, kHeapSamplerSlots};
  const VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
  for (uint32_t i = 0; i < 4; ++i) {
    VkDescriptorSetLayoutBinding binding{0, i == 3 ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        counts[i], VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    binding_flags.bindingCount = 1; binding_flags.pBindingFlags = &flags;
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &binding_flags};
    info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    info.bindingCount = 1; info.pBindings = &binding;
    if ((result = vkCreateDescriptorSetLayout(vulkan->vk, &info, nullptr, &draw_sets[i])) != VK_SUCCESS)
      return GraphicsError(evidence, "native-vulkan-draw-set-layout", result);
  }
  const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, counts[0] + counts[1] + counts[2]},
                                     {VK_DESCRIPTOR_TYPE_SAMPLER, counts[3]}};
  VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
  pool.maxSets = 4; pool.poolSizeCount = 2; pool.pPoolSizes = sizes;
  if ((result = vkCreateDescriptorPool(vulkan->vk, &pool, nullptr, &heap_pool)) != VK_SUCCESS)
    return GraphicsError(evidence, "native-vulkan-heap-pool", result);
  VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate.descriptorPool = heap_pool; allocate.descriptorSetCount = 4; allocate.pSetLayouts = draw_sets.data();
  if ((result = vkAllocateDescriptorSets(vulkan->vk, &allocate, heap_sets.data())) != VK_SUCCESS)
    return GraphicsError(evidence, "native-vulkan-heap-sets", result);
  // Slot 0: null descriptors, or zero RGBA texels (cleared by the first draw)
  // on devices without robustness2. Separate dimensions make every statically
  // used descriptor type valid.
  null_descriptors = vulkan->nullDescriptorSupported;
#if defined(NATIVE_RENDERER_ENGINE_FAULT_TEST)
  if (const char* force = std::getenv("REX_NATIVE_TEST_NULL_FALLBACK"); force && std::string_view(force) == "1")
    null_descriptors = false;
#endif
  for (unsigned i = 0; i < 3; ++i) {
    if (!null_descriptors) {
      result = CreateImage(vulkan->allocator, VK_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, i == 2, fallback_images[i], 0,
                           i == 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D);
      if (result != VK_SUCCESS) return GraphicsError(evidence, "native-vulkan-fallback-image", result);
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view.image = fallback_images[i]->image; view.format = VK_FORMAT_R8G8B8A8_UNORM;
      view.viewType = i == 1 ? VK_IMAGE_VIEW_TYPE_3D : i == 2 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
      view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, i == 2 ? 6u : 1u};
      if ((result = vkCreateImageView(vulkan->vk, &view, nullptr, &fallback_views[i])) != VK_SUCCESS)
        return GraphicsError(evidence, "native-vulkan-fallback-view", result);
    }
    WriteHeap(i, 0, fallback_views[i], VK_NULL_HANDLE);
  }
  VkSamplerCreateInfo point{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  point.maxLod = VK_LOD_CLAMP_NONE;
  uint32_t slot = 0;
  if ((result = HeapSampler(point, slot)) != VK_SUCCESS || slot)
    return GraphicsError(evidence, "native-vulkan-default-sampler", result);
  return true;
}

void Backend::State::WriteHeap(uint32_t set, uint32_t slot, VkImageView view, VkSampler sampler) {
  const VkDescriptorImageInfo image{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = heap_sets[set]; write.dstArrayElement = slot; write.descriptorCount = 1;
  write.descriptorType = set == 3 ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  write.pImageInfo = &image;
  vkUpdateDescriptorSets(vulkan->vk, 1, &write, 0, nullptr);
}

VkResult Backend::State::HeapView(HeapViews& views, VkImage image, VkFormat format, uint32_t levels, bool cube,
                                  const ComponentMapping& mapping, uint32_t& slot) {
  uint32_t key = cube ? 1u << 12 : 0;
  for (unsigned i = 0; i < 4; ++i) key |= uint32_t(mapping[i]) << (i * 3);
  for (const auto& v : views.views)
    if (v.key == key) { slot = v.slot; return VK_SUCCESS; }
  if (!(slot = AllocateSlot())) return VK_ERROR_TOO_MANY_OBJECTS;
  VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  info.image = image; info.format = format;
  info.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
  info.components = ImageViewMapping(mapping);
  info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, cube ? 6u : 1u};
  VkImageView view{};
  const VkResult result = vkCreateImageView(vulkan->vk, &info, nullptr, &view);
  if (result != VK_SUCCESS) {
    free_views.push_back(slot);  // Never written.
    slot = 0;
    return result;
  }
  views.device = vulkan->vk;
  views.free_slots = &free_views;
  views.views.push_back({key, view, slot});
  WriteHeap(cube ? 2 : 0, slot, view, VK_NULL_HANDLE);
  draw_counters.heap_views.fetch_add(1, std::memory_order_relaxed);
  return VK_SUCCESS;
}

VkResult Backend::State::HeapSampler(const VkSamplerCreateInfo& info, uint32_t& slot) {
  std::array<uint64_t, 21> key;
  if (!SamplerKey(info, key)) return VK_ERROR_FEATURE_NOT_PRESENT;
  if (auto it = heap_samplers.find(key); it != heap_samplers.end()) { slot = it->second.second; return VK_SUCCESS; }
  if (heap_samplers.size() >= kHeapSamplerSlots) return VK_ERROR_TOO_MANY_OBJECTS;
  VkSampler sampler{};
  const VkResult result = vkCreateSampler(vulkan->vk, &info, nullptr, &sampler);
  if (result != VK_SUCCESS) return result;
  slot = uint32_t(heap_samplers.size());
  heap_samplers.emplace(key, std::make_pair(sampler, slot));
  WriteHeap(3, slot, VK_NULL_HANDLE, sampler);
  return VK_SUCCESS;
}

bool Backend::PreparePipelines(std::string& evidence) {
  if (!state_->ready || state_->owner_thread != GetCurrentThreadId())
    return GraphicsError(evidence, "native-prepare-owner");
  if (!state_->EnsureGraphics(evidence)) return false;
  evidence = "{\"prepared\":true,\"backend\":\"vulkan\"}";
  return true;
}
bool Backend::PrecompilePipeline(const PipelineDesc& desc, bool& present, std::string& evidence) {
  present = false;
  if (!state_->draw_layout || !desc.elements) return GraphicsError(evidence, "native-precompile-not-prepared");
  PipelineDesc d = desc;
  d.state[8] = state_->render_mode.Samples(d.state[8]);
  const auto result = state_->pipeline_set.Precompile(VulkanPipelineKey(d), d, present);
  if (!result.ok) return GraphicsError(evidence, result.reason, VkResult(result.code));
  evidence = fmt::format("{{\"precompiled\":true,\"present\":{}}}", present);
  return true;
}
bool Backend::PostPipeline(const PipelineDesc& desc) {
  if (!state_->draw_layout || !desc.elements) return false;
  PipelineDesc d = desc; d.state[8] = state_->render_mode.Samples(d.state[8]);
  const uint64_t key = VulkanPipelineKey(d);
  return state_->pipeline_set.Post(key, std::move(d), true, false);
}
void Backend::PrefetchPipelines(const std::vector<DrawRequest>& requests) {
  for (const auto& r : requests) {
    if (r.pipeline_take != DrawRequest::PipelineTake::kPrefetch || !r.elements) continue;
    auto color = state_->surfaces.find(r.color_key), depth = state_->surfaces.find(r.depth_key);
    const bool has_color = color != state_->surfaces.end();
    const bool has_depth = (r.depth_enable || r.stencil_enable) && depth != state_->surfaces.end();
    if (!has_color && !has_depth) continue;
    PipelineDesc d{r.vs, r.ps, r.elements, r.layout_id ? r.layout_id : ElementsId(*r.elements)};
    if (DrawStateWords(r, has_color ? color->second.desc.samples : depth->second.desc.samples,
                       has_depth, has_color, d.state)) PostPipeline(d);
  }
}
std::vector<PipelineDesc> Backend::PipelineDescs() {
  std::vector<PipelineDesc> out;
  for (const auto& [key, d] : state_->pipeline_descs) out.push_back(d);
  return out;
}
std::string Backend::PipelineList() {
  std::string out = "[";
  for (const auto& [key, d] : state_->pipeline_descs) {
    if (out.size() > 1) out += ",";
    std::string elements;
    if (d.elements) for (const auto& e : *d.elements)
      elements += fmt::format("{}[\"{}\",{},{},{},{}{}]", elements.empty() ? "" : ",",
          e.semantic, e.semantic_index, uint32_t(e.format), e.slot, e.offset,
          e.instance_step ? fmt::format(",{}", e.instance_step) : std::string());
    out += fmt::format("{{\"key\":\"{:016x}\",\"backend_key\":\"{:016x}\",\"vs\":\"{}\",\"ps\":\"{}\","
        "\"layout\":\"{:016x}\",\"elements\":[{}],\"state\":[{}],\"origin\":\"{}\",\"create_us\":{},\"world\":\"{:08x}\","
        "\"vs_spec\":[{},{}],\"ps_spec\":[{},{}]}}",
        PipelineKey(d.vs.key, d.ps.key, d.layout_id, d.state), key, d.vs.key, d.ps.key, d.layout_id, elements,
        fmt::join(d.state, ","), state_->pipeline_origins[key], state_->pipeline_create_us[key], state_->pipeline_worlds[key],
        d.vs.spec_mask, d.vs.spec_value,
        d.ps.spec_mask, d.ps.spec_value);
  }
  return out + "]";
}

bool Backend::DrawIndexed(const DrawRequest& r, uint64_t& fence, std::string& evidence) {
  fence = 0;
  auto fail = [&](const char* reason, VkResult code = VK_ERROR_UNKNOWN) { return GraphicsError(evidence, reason, code); };
  auto& s = *state_;
  if (!s.ready || s.owner_thread != GetCurrentThreadId() || s.operation_poisoned || s.unproved_execution)
    return fail("native-draw-owner-or-state");
  State::Surface *color = nullptr, *depth = nullptr;
  if (!r.depth_only || r.color_key) {
    auto it = s.surfaces.find(r.color_key);
    if (it == s.surfaces.end() || it->second.desc.format != SurfaceFormat::Color8888) return fail("native-draw-color-surface-unknown");
    color = &it->second;
  }
  if (r.depth_enable || r.stencil_enable) {
    auto it = s.surfaces.find(r.depth_key);
    if (it == s.surfaces.end() || it->second.desc.format != SurfaceFormat::DepthStencil) return fail("native-draw-depth-surface");
    depth = &it->second;
    if (color && (depth->desc.samples != color->desc.samples || depth->desc.width < color->desc.width ||
                  depth->desc.height < color->desc.height)) return fail("native-draw-depth-surface");
  }
  if (r.depth_only && !depth) return fail("native-draw-depth-only-without-depth");
  if (!r.vs.Present() || r.vs.format != ShaderFormat::kSpirv ||
      (r.depth_only ? bool(r.ps.bytes) : !r.ps.Present() || r.ps.format != ShaderFormat::kSpirv))
    return fail("native-draw-shaders");
  const auto& target = color ? *color : *depth;
  if (!r.index_count || !r.elements || r.elements->empty() || r.streams.empty() || r.streams.size() > 8)
    return fail("native-draw-vertex-input");
  for (const auto& e : *r.elements)
    if (e.slot >= r.streams.size() || !r.streams[e.slot].stride) return fail("native-draw-vertex-element");
  if (!s.EnsureGraphics(evidence)) return false;
  PipelineDesc desc{r.vs, r.ps, r.elements, r.layout_id ? r.layout_id : ElementsId(*r.elements)};
  if (!DrawStateWords(r, target.desc.samples, depth != nullptr, color != nullptr, desc.state))
    return fail("native-draw-topology-uncovered");
  const uint64_t key = VulkanPipelineKey(desc), triple = TripleKey(r.vs.key, r.ps.key, desc.layout_id);
  auto& pipeline = s.pipelines[key];
  const bool created = !pipeline;
  if (!pipeline) {
    // Normal and diagnostic draws both remain visible on Vulkan. Never skip
    // a draw because a pipeline compilation is pending.
    auto take = s.pipeline_set.Take(key, triple, r.pipeline_take == DrawRequest::PipelineTake::kSkip ?
        DrawRequest::PipelineTake::kPrefetch : r.pipeline_take);
    pipeline = std::move(take.pipeline);
    s.pipeline_origins[key] = take.origin ? take.origin : "created";
    if (!pipeline) {
      const auto begin = std::chrono::steady_clock::now();
      const auto result = MakeGraphicsPipeline(s.vulkan->vk, s.draw_layout, desc, s.vertex_locations, pipeline);
      if (!result.ok) { s.pipelines.erase(key); return fail(result.reason, VkResult(result.code)); }
      const auto micros = uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count());
      s.pipeline_create_us[key] = micros;
      s.pipeline_origins[key] = s.pipeline_set.NoteCreated(take, triple, micros);
    }
    s.pipeline_worlds[key] = WorldKey();
    s.pipeline_descs.emplace(key, desc);
  }
  if (!ApplyDrawConstants(r.vs_constants, r.vs_constants_delta, s.vs_block) ||
      !ApplyDrawConstants(r.ps_constants, r.ps_constants_delta, s.ps_block)) return fail("native-draw-constants");
  // Uploads may close a full batch, so perform all of them before recording
  // this draw and retaining its resources.
  for (const auto& t : r.textures) if (!t.resolved && !t.data.empty())
    if (!UploadSampled(t, evidence)) return false;
  const char* reason = nullptr; VkResult result = VK_SUCCESS;
  if (!s.OpenBatch(reason, result)) return fail(reason, result);
  s.batch.list->setFramebuffer(nullptr);
  const auto commands = s.DrawCommands();
  if (!s.batch.graphics) {
    if (!s.free_draw_caches.empty()) {
      s.batch.graphics = std::move(s.free_draw_caches.back()); s.free_draw_caches.pop_back();
    } else s.batch.graphics = std::make_unique<DrawCache>(s.vulkan->vk, s.draw_counters);
  }
  auto& resources = *s.batch.graphics;
  s.batch.retained.push_back(pipeline);
  if (!s.null_descriptors && !s.fallback_initialized) {
    s.EndDrawPass();
    // The zero RGBA texels of the heap's slot 0 (CreateHeap) reproduce null
    // sampling on devices without robustness2.
    for (unsigned i = 0; i < 3; ++i) {
      SyncState sync;
      Transition(commands, s.fallback_images[i]->image, VK_IMAGE_ASPECT_COLOR_BIT, sync, kCopyDest);
      const VkClearColorValue zero{};
      const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, i == 2 ? 6u : 1u};
      vkCmdClearColorImage(commands, s.fallback_images[i]->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
      Transition(commands, s.fallback_images[i]->image, VK_IMAGE_ASPECT_COLOR_BIT, sync, kShaderRead);
    }
    s.fallback_initialized = true;
  }
  // The shaders read each texture's heap slots from the shared constants;
  // the registers the draw does not bind keep slot 0 (null texture, point sampler).
  auto shared = r.shared_constants;
  // Descriptor index fields only: preserve booleans, fetch descriptors and planes.
  std::fill(shared.begin(), shared.begin() + 256, 0);
  std::fill(shared.begin() + 288, shared.begin() + 320, 0);
  const auto& scale = s.render_mode.scale;
  const float inverse[]{1.0f / scale.x, 1.0f / scale.y};
  std::memcpy(shared.data() + 280, inverse, 8);
  for (const auto& t : r.textures) {
    if (t.reg >= 20) return fail("native-draw-texture-register");
    VkImage image{}; VkFormat format{}; uint32_t levels = 1;
    HeapViews* views = nullptr;
    if (t.resolved) {
      auto it = s.textures.find(t.key);
      if (it == s.textures.end() || !it->second.content_defined) return fail("native-draw-resolve-texture-undefined");
      auto& texture = it->second;
      image = texture.Image(); format = texture.depth ? VK_FORMAT_R32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
      s.DrawTransition(image, VK_IMAGE_ASPECT_COLOR_BIT, texture.state, kShaderRead);
      texture.last_use = s.batch.sequence;
      views = &texture.views;
    } else {
      auto it = s.sampled.find(t.key);
      if (it == s.sampled.end() || it->second.version < t.version) return fail("native-draw-sampled-copy-stale");
      auto& texture = it->second;
      image = texture.image->image; format = ImageFormat(texture.format); levels = texture.levels;
      s.DrawTransition(image, VK_IMAGE_ASPECT_COLOR_BIT, texture.state, kShaderRead);
      texture.last_use = s.batch.sequence;
      views = &texture.views;
    }
    uint32_t slot = 0;
    if ((result = s.HeapView(*views, image, format, levels, t.cube, t.component_mapping, slot)) != VK_SUCCESS)
      return fail(result == VK_ERROR_TOO_MANY_OBJECTS ? "native-vulkan-heap-views-exhausted" :
                  "native-vulkan-sampled-view", result);
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.minFilter = t.min_filter == Filter::kLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sampler.magFilter = t.mag_filter == Filter::kLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sampler.mipmapMode = t.mip_filter == Filter::kLinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    auto address = [](AddressMode mode) {
      constexpr VkSamplerAddressMode modes[]{VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
          VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE};
      return modes[uint32_t(mode)];
    };
    sampler.addressModeU = address(t.address_u); sampler.addressModeV = address(t.address_v); sampler.addressModeW = address(t.address_w);
    const auto& limits = s.vulkan->physicalDeviceProperties.limits;
    sampler.mipLodBias = std::clamp(t.lod_bias, -limits.maxSamplerLodBias, limits.maxSamplerLodBias);
    sampler.minLod = t.min_lod; sampler.maxLod = std::max(t.max_lod, t.min_lod);
    sampler.anisotropyEnable = t.anisotropic;
    sampler.maxAnisotropy = std::clamp(float(t.max_anisotropy), 1.0f, limits.maxSamplerAnisotropy);
    VkSamplerCustomBorderColorCreateInfoEXT border{VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};
    if (t.border_color == std::array<float, 4>{0, 0, 0, 0}) sampler.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    else if (t.border_color == std::array<float, 4>{0, 0, 0, 1}) sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    else if (t.border_color == std::array<float, 4>{1, 1, 1, 1}) sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    else {
      if (!s.custom_border)
        return fail("native-vulkan-custom-border-color-required", VK_ERROR_FEATURE_NOT_PRESENT);
      std::copy(t.border_color.begin(), t.border_color.end(), border.customBorderColor.float32);
      sampler.pNext = &border; sampler.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
    }
    uint32_t sampler_slot = 0;
    if ((result = s.HeapSampler(sampler, sampler_slot)) != VK_SUCCESS)
      return fail(result == VK_ERROR_TOO_MANY_OBJECTS ? "native-vulkan-heap-samplers-exhausted" :
                  "native-vulkan-sampler", result);
    const uint32_t image_offset = t.reg < 16 ? (t.cube ? 128 : 0) + t.reg * 4 : 288 + (t.reg - 16) * 4;
    const uint32_t sampler_offset = t.reg < 16 ? 192 + t.reg * 4 : 304 + (t.reg - 16) * 4;
    std::memcpy(shared.data() + image_offset, &slot, 4); std::memcpy(shared.data() + sampler_offset, &sampler_slot, 4);
  }
  // Each draw gets one contiguous allocation; offsets are stable even if the
  // upload page vector grows on the next draw.
  auto align = [](size_t n) { return (n + 511) & ~size_t(511); };
  const bool vs_reuse = r.vs_constants_version && s.batch.vs_constants && s.batch.vs_version == r.vs_constants_version;
  const bool ps_reuse = r.ps_constants_version && s.batch.ps_constants && s.batch.ps_version == r.ps_constants_version;
  size_t bytes = 0;
  const size_t vs_at = bytes; if (!vs_reuse) bytes += 4096;
  const size_t ps_at = bytes; if (!ps_reuse) bytes += 4096;
  const size_t shared_at = bytes; bytes += 512;
  s.draw_counters.constant_bytes.fetch_add(bytes, std::memory_order_relaxed);
  std::array<size_t, 8> offsets{};
  for (size_t i = 0; i < r.streams.size(); ++i) {
    offsets[i] = bytes;
    if (!r.streams[i].key) bytes = align(bytes + r.streams[i].data.size());
  }
  const size_t index_at = bytes, index_bytes = size_t(r.index_count) * (r.index32 ? 4 : 2);
  if (!r.index_key) {
    if (r.indices.size() < index_bytes) return fail("native-draw-indices");
    bytes += index_bytes;
  }
  State::UploadPage* page{}; VkDeviceSize base{};
  if (!s.Upload(bytes, page, base, result)) return fail("native-draw-upload", result);
  auto* cpu = page->buffer->mapped + base;
  if (!vs_reuse) std::memcpy(cpu + vs_at, s.vs_block.data(), 4096);
  if (!ps_reuse) std::memcpy(cpu + ps_at, s.ps_block.data(), 4096);
  std::memcpy(cpu + shared_at, shared.data(), 512);
  const auto gpu = page->address + base;
  if (!vs_reuse) s.batch.vs_constants = gpu + vs_at;
  if (!ps_reuse) s.batch.ps_constants = gpu + ps_at;
  s.batch.vs_version = r.vs_constants_version; s.batch.ps_version = r.ps_constants_version;
  const uint64_t constants[]{s.batch.vs_constants, s.batch.ps_constants, gpu + shared_at};
  std::array<VkBuffer, 8> streams{};
  std::array<VkDeviceSize, 8> stream_offsets{}, strides{};
  for (size_t i = 0; i < r.streams.size(); ++i) {
    const auto& stream = r.streams[i]; strides[i] = stream.stride;
    if (stream.key) {
      auto it = s.buffers.find(stream.key);
      if (it == s.buffers.end() || !stream.bytes || stream.offset > it->second.bytes ||
          stream.bytes > it->second.bytes - stream.offset) return fail("native-draw-stream-buffer");
      auto& b = it->second;
      streams[i] = b.buffer->buffer; stream_offsets[i] = stream.offset;
      s.DrawTransition(streams[i], b.state, kBufferDraw); b.last_use = s.batch.sequence;
    } else {
      streams[i] = page->buffer->buffer; stream_offsets[i] = base + offsets[i];
      if (!stream.data.empty()) std::memcpy(cpu + offsets[i], stream.data.data(), stream.data.size());
    }
  }
  VkBuffer indices = page->buffer->buffer; VkDeviceSize index_offset = base + index_at;
  if (r.index_key) {
    auto it = s.buffers.find(r.index_key);
    if (it == s.buffers.end() || r.index_offset > it->second.bytes || index_bytes > it->second.bytes - r.index_offset)
      return fail("native-draw-index-buffer");
    auto& b = it->second; indices = b.buffer->buffer; index_offset = r.index_offset;
    s.DrawTransition(indices, b.state, kBufferDraw); b.last_use = s.batch.sequence;
  } else std::memcpy(cpu + index_at, r.indices.data(), index_bytes);
  const VkImage color_image = color ? color->Image() : VK_NULL_HANDLE;
  const VkImage depth_image = depth ? depth->Image() : VK_NULL_HANDLE;
  if (s.batch.draw_pass_active && (s.batch.draw_color != color_image || s.batch.draw_depth != depth_image))
    s.EndDrawPass();
  auto* p = static_cast<GraphicsPipeline*>(pipeline.get());
  if (!s.batch.draw_pass_active) {
    std::vector<VkImageView> attachments;
    for (auto* surface : {color, depth}) if (surface) {
      const bool is_depth = surface == depth;
      Transition(commands, surface->Image(), surface->Aspect(), surface->state, is_depth ? kDepthTarget : kColorTarget);
      VkImageView view{};
      if ((result = resources.View(surface->Image(), is_depth ? VK_FORMAT_D32_SFLOAT_S8_UINT : VK_FORMAT_R8G8B8A8_UNORM,
          surface->Aspect(), 1, false, kIdentityMapping, view)) != VK_SUCCESS) return fail("native-vulkan-target-view", result);
      attachments.push_back(view);
    }
    VkFramebuffer framebuffer{};
    const uint32_t compatibility = target.desc.samples | (color ? 0x100 : 0) | (depth ? 0x200 : 0);
    if ((result = resources.Framebuffer(p->pass, attachments, target.desc.width, target.desc.height, compatibility, framebuffer)) != VK_SUCCESS)
      return fail("native-vulkan-draw-framebuffer", result);
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = p->pass; begin.framebuffer = framebuffer;
    begin.renderArea.extent = {target.desc.width, target.desc.height};
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    s.batch.draw_pass_active = true;
    s.batch.draw_color = color_image; s.batch.draw_depth = depth_image;
    s.draw_counters.passes.fetch_add(1, std::memory_order_relaxed);
  }
  if (color) color->last_use = s.batch.sequence;
  if (depth) depth->last_use = s.batch.sequence;
  VkViewport viewport{r.viewport[0] * scale.x, r.viewport[1] * scale.y, r.viewport[2] * scale.x,
                       r.viewport[3] * scale.y, r.viewport[4], r.viewport[5]};
  const int32_t left = std::clamp(scale.X(r.scissor[0]), 0, int32_t(target.desc.width));
  const int32_t top = std::clamp(scale.Y(r.scissor[1]), 0, int32_t(target.desc.height));
  VkRect2D scissor{{left, top}, {uint32_t(std::max(0, std::min(scale.X(r.scissor[2]), int32_t(target.desc.width)) - left)),
                                uint32_t(std::max(0, std::min(scale.Y(r.scissor[3]), int32_t(target.desc.height)) - top))}};
  auto& bound = s.batch;
  const bool valid = bound.draw_bindings_valid;
  if (!valid || bound.draw_pipeline != p->pipeline) {
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
    bound.draw_pipeline = p->pipeline;
    s.draw_counters.pipeline_binds.fetch_add(1, std::memory_order_relaxed);
  }
  if (!valid || std::memcmp(&viewport, &bound.draw_viewport, sizeof(viewport))) {
    vkCmdSetViewport(commands, 0, 1, &viewport); bound.draw_viewport = viewport;
  }
  if (!valid || std::memcmp(&scissor, &bound.draw_scissor, sizeof(scissor))) {
    vkCmdSetScissor(commands, 0, 1, &scissor); bound.draw_scissor = scissor;
  }
  if (!valid || r.blend_factor != bound.draw_blend) {
    vkCmdSetBlendConstants(commands, r.blend_factor.data()); bound.draw_blend = r.blend_factor;
  }
  if (!valid || r.stencil_ref != bound.draw_stencil) {
    vkCmdSetStencilReference(commands, VK_STENCIL_FACE_FRONT_AND_BACK, r.stencil_ref); bound.draw_stencil = r.stencil_ref;
  }
  if (!valid || r.topology != bound.draw_topology) {
    vkCmdSetPrimitiveTopologyEXT(commands, r.topology == Topology::kTriangleStrip ?
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    bound.draw_topology = r.topology;
  }
  if (!valid || bound.draw_stream_count != r.streams.size() || bound.draw_streams != streams ||
      bound.draw_offsets != stream_offsets || bound.draw_strides != strides) {
    vkCmdBindVertexBuffers2EXT(commands, 0, uint32_t(r.streams.size()), streams.data(), stream_offsets.data(), nullptr, strides.data());
    bound.draw_stream_count = uint32_t(r.streams.size()); bound.draw_streams = streams;
    bound.draw_offsets = stream_offsets; bound.draw_strides = strides;
  }
  if (!valid || bound.draw_indices != indices || bound.draw_index_offset != index_offset || bound.draw_index32 != r.index32) {
    vkCmdBindIndexBuffer(commands, indices, index_offset, r.index32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
    bound.draw_indices = indices; bound.draw_index_offset = index_offset; bound.draw_index32 = r.index32;
  }
  // The heap is bound once per pass: transfers and the passes of other layouts end it.
  if (!valid) {
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, s.draw_layout, 0, 4, s.heap_sets.data(), 0, nullptr);
    s.draw_counters.descriptor_binds.fetch_add(1, std::memory_order_relaxed);
  }
  vkCmdPushConstants(commands, s.draw_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 24, constants);
  vkCmdDrawIndexed(commands, r.index_count, r.instance_count, 0, r.base_vertex, 0);
  bound.draw_bindings_valid = true;
  fence = s.batch.sequence; ++s.operations;
  s.draw_counters.draws.fetch_add(1, std::memory_order_relaxed);
  if (++s.batch.operations >= kBatchOperationsMax && !s.CloseBatch(reason, result)) return fail(reason, result);
  if (r.want_evidence) evidence = fmt::format("{{\"drawn\":true,\"fence\":{},\"samples\":{},\"pipeline_created\":{},"
      "\"pipeline\":\"{:016x}\",\"fallback_descriptors\":{}}}", fence, target.desc.samples, created, key, !s.null_descriptors);
  else evidence.clear();
  return true;
}
