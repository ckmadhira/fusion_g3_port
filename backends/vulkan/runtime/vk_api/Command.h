/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

// @lint-ignore-every CLANGTIDY facebook-hte-BadMemberName

#include <executorch/backends/vulkan/runtime/vk_api/vk_api.h>

#include <executorch/backends/vulkan/runtime/utils/VecUtils.h>

#include <executorch/backends/vulkan/runtime/vk_api/Descriptor.h>
#include <executorch/backends/vulkan/runtime/vk_api/Pipeline.h>
#include <executorch/backends/vulkan/runtime/vk_api/Shader.h>

#include <executorch/backends/vulkan/runtime/vk_api/memory/Buffer.h>
#include <executorch/backends/vulkan/runtime/vk_api/memory/Image.h>

namespace vkcompute {
namespace vkapi {

class CommandBuffer final {
 public:
  explicit CommandBuffer(
      VkCommandBuffer,
      VkSemaphore,
      const VkCommandBufferUsageFlags);

  CommandBuffer(const CommandBuffer&) = delete;
  CommandBuffer& operator=(const CommandBuffer&) = delete;

  CommandBuffer(CommandBuffer&&) noexcept;
  CommandBuffer& operator=(CommandBuffer&&) noexcept;

  ~CommandBuffer() = default;

  // The lifecycle of a command buffer is as follows:
  enum State {
    INVALID, // Used to indicate the command buffer is moved from
    NEW, // Set during constructor
    RECORDING, // Set during call to begin() and dispatch()
    PIPELINE_BOUND, // Set during call to  bind_pipeline()
    DESCRIPTORS_BOUND, // Set during call to bind_descriptors()
    BARRIERS_INSERTED, // Set during call to insert_barrier()
    READY, //  Set during call to end()
    SUBMITTED, // Set during call to get_submit_handle()
  };

  struct Bound {
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
    utils::WorkgroupSize local_workgroup_size;
    VkDescriptorSet descriptors;

    explicit Bound()
        : pipeline{VK_NULL_HANDLE},
          pipeline_layout{VK_NULL_HANDLE},
          local_workgroup_size{0u, 0u, 0u},
          descriptors{VK_NULL_HANDLE} {}

    inline void reset() {
      pipeline = VK_NULL_HANDLE;
      pipeline_layout = VK_NULL_HANDLE;
      local_workgroup_size = utils::WorkgroupSize{0u, 0u, 0u};
      descriptors = VK_NULL_HANDLE;
    }
  };

 private:
  VkCommandBuffer handle_;
  // Semaphore to signal when the command buffer has completed execution
  VkSemaphore signal_semaphore_;
  VkCommandBufferUsageFlags flags_;
  State state_;
  Bound bound_;

 public:
  inline bool is_reusable() {
    return !(flags_ & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
  }

  inline void invalidate() {
    handle_ = VK_NULL_HANDLE;
    signal_semaphore_ = VK_NULL_HANDLE;
    bound_.reset();
  }

  void begin();
  void end();

  void bind_pipeline(VkPipeline, VkPipelineLayout, const utils::WorkgroupSize);
  void bind_descriptors(VkDescriptorSet);
  void set_push_constants(VkPipelineLayout, const void*, uint32_t);

  void insert_barrier(PipelineBarrier& pipeline_barrier);
  void dispatch(const utils::uvec3&);
  void blit(vkapi::VulkanImage& src, vkapi::VulkanImage& dst);

  void write_timestamp(VkQueryPool, const uint32_t) const;
  void reset_querypool(VkQueryPool, const uint32_t, const uint32_t) const;

  VkCommandBuffer get_submit_handle(const bool final_use = false);

  VkSemaphore get_signal_semaphore() const {
    return signal_semaphore_;
  }

  inline operator bool() const {
    return handle_ != VK_NULL_HANDLE;
  }
};

struct CommandPoolConfig final {
  uint32_t cmd_pool_initial_size;
  uint32_t cmd_pool_batch_size;
};

class CommandPool final {
 public:
  explicit CommandPool(VkDevice, const uint32_t, const CommandPoolConfig&);

  CommandPool(const CommandPool&) = delete;
  CommandPool& operator=(const CommandPool&) = delete;

  CommandPool(CommandPool&&) = delete;
  CommandPool& operator=(CommandPool&&) = delete;

  ~CommandPool();

 private:
  VkDevice device_;
  uint32_t queue_family_idx_;
  VkCommandPool pool_;
  CommandPoolConfig config_;
  // New Buffers
  std::mutex mutex_;
  std::vector<VkCommandBuffer> buffers_;
  // Semaphores corresponding to the command buffers
  std::vector<VkSemaphore> semaphores_;
  size_t in_use_;

 public:
  CommandBuffer get_new_cmd(bool reusable = false);

  void flush();

 private:
  void allocate_new_batch(const uint32_t);
};

} // namespace vkapi
} // namespace vkcompute
