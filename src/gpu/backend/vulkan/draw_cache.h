// The per-batch objects of the draw passes: the attachment views and the
// framebuffers, shared within one GPU batch. The batch owns this cache until
// its submission completes, before any retired images are freed. The sampled
// textures and the samplers are in the draw heap instead (backend.cpp).
#pragma once
#include "gpu/backend/vulkan/graphics.h"
#include <unordered_map>

namespace gpu::vulkan {
struct DrawCacheCounters {
  std::atomic<uint64_t> draws{0}, views{0}, framebuffers{0}, constant_bytes{0}, passes{0};
  std::atomic<uint64_t> pipeline_binds{0}, descriptor_binds{0};
  std::atomic<uint64_t> heap_views{0};  // Views written into the draw heap.
};
template <size_t N> struct DrawKeyHash {
  size_t operator()(const std::array<uint64_t, N>& key) const {
    uint64_t h = 0xCBF29CE484222325ull;
    for (auto word : key) { h ^= word; h *= 0x100000001B3ull; }
    // Handles are commonly aligned pointers. Mix their high bits into low
    // bits too: MSVC's unordered_map uses power-of-two bucket counts.
    h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27; h *= 0x94D049BB133111EBull;
    return size_t(h ^ (h >> 31));
  }
};
// The key of a sampler: explicit fields, never structure padding or the
// address of pNext. False for a pNext other than one custom border color.
inline bool SamplerKey(const VkSamplerCreateInfo& info, std::array<uint64_t, 21>& key) {
  key = {info.flags, uint64_t(info.magFilter), uint64_t(info.minFilter), uint64_t(info.mipmapMode),
      uint64_t(info.addressModeU), uint64_t(info.addressModeV), uint64_t(info.addressModeW),
      std::bit_cast<uint32_t>(info.mipLodBias), info.anisotropyEnable, std::bit_cast<uint32_t>(info.maxAnisotropy),
      info.compareEnable, uint64_t(info.compareOp), std::bit_cast<uint32_t>(info.minLod),
      std::bit_cast<uint32_t>(info.maxLod), uint64_t(info.borderColor), info.unnormalizedCoordinates};
  if (info.pNext) {
    const auto* border = static_cast<const VkSamplerCustomBorderColorCreateInfoEXT*>(info.pNext);
    if (border->sType != VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT || border->pNext) return false;
    key[16] = uint64_t(border->format);
    for (unsigned i = 0; i < 4; ++i) {
      uint32_t bits;
      std::memcpy(&bits, reinterpret_cast<const uint8_t*>(&border->customBorderColor) + i * 4, 4);
      key[17 + i] = bits;
    }
  }
  return true;
}
class DrawCache {
 public:
  DrawCache(VkDevice device, DrawCacheCounters& counters) : device_(device), counters_(counters) {}
  DrawCache(const DrawCache&) = delete;
  DrawCache& operator=(const DrawCache&) = delete;
  ~DrawCache() { Recycle(); }
  // Called only after the batch's GPU fence completes.
  void Recycle() {
    for (const auto& [key, framebuffer] : framebuffers_) vkDestroyFramebuffer(device_, framebuffer, nullptr);
    for (const auto& [key, view] : views_) vkDestroyImageView(device_, view, nullptr);
    framebuffers_.clear(); views_.clear();
  }
  VkResult View(VkImage image, VkFormat format, VkImageAspectFlags aspect, uint32_t levels,
                bool cube, const ComponentMapping& mapping, VkImageView& out) {
    uint32_t swizzle = 0;
    for (unsigned i = 0; i < 4; ++i) swizzle |= uint32_t(mapping[i]) << (i * 3);
    const ViewKey key{std::bit_cast<uint64_t>(image), uint64_t(format), aspect, levels, cube, swizzle};
    if (auto it = views_.find(key); it != views_.end()) { out = it->second; return VK_SUCCESS; }
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = image; info.format = format;
    info.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    info.components = ImageViewMapping(mapping);
    info.subresourceRange = {aspect, 0, levels, 0, cube ? 6u : 1u};
    const auto result = vkCreateImageView(device_, &info, nullptr, &out);
    if (result == VK_SUCCESS) { views_.emplace(key, out); counters_.views.fetch_add(1, std::memory_order_relaxed); }
    return result;
  }
  VkResult Framebuffer(VkRenderPass pass, const std::vector<VkImageView>& attachments,
                        uint32_t width, uint32_t height, uint32_t compatibility, VkFramebuffer& out) {
    // All draw render passes come from MakeRenderPass: fixed RGBA8/D32S8,
    // matching subpass structure. Only attachment presence and samples vary.
    const FrameKey key{compatibility,
        attachments.empty() ? 0 : std::bit_cast<uint64_t>(attachments[0]),
        attachments.size() < 2 ? 0 : std::bit_cast<uint64_t>(attachments[1]), width, height};
    if (auto it = framebuffers_.find(key); it != framebuffers_.end()) { out = it->second; return VK_SUCCESS; }
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = pass; info.attachmentCount = uint32_t(attachments.size()); info.pAttachments = attachments.data();
    info.width = width; info.height = height; info.layers = 1;
    const auto result = vkCreateFramebuffer(device_, &info, nullptr, &out);
    if (result == VK_SUCCESS) { framebuffers_.emplace(key, out); counters_.framebuffers.fetch_add(1, std::memory_order_relaxed); }
    return result;
  }
 private:
  using ViewKey = std::array<uint64_t, 6>;
  using FrameKey = std::array<uint64_t, 5>;
  const VkDevice device_;
  DrawCacheCounters& counters_;
  std::unordered_map<ViewKey, VkImageView, DrawKeyHash<6>> views_;
  std::unordered_map<FrameKey, VkFramebuffer, DrawKeyHash<5>> framebuffers_;
};
}  // namespace gpu::vulkan
