---
id: I032
kind: instrument
status: DISTRUSTED
created: 2026-08-12
distrusted_on: 2026-08-12
---

## Instrument

Vulkan validation object-leak report at vkDestroyDevice (enabled by ZELDA3D_SDL3GPU_DEBUG=1, which passes debug=true to SDL_CreateGPUDevice)

## Validated by

Counts confirmed against an uncapped run: default output is TRUNCATED at 10 messages per VUID, so it reported 10 leaked objects where there were 409. Trustworthy only with khronos_validation.duplicate_message_limit = 0 via VK_LAYER_SETTINGS_PATH.

## Known failure modes

(none recorded yet)

## DISTRUSTED 2026-08-12

Truncates silently-ish at 10 messages per VUID (duplicate_message_limit), so it reported 5 VkImage + 5 VkBuffer where the real figure is 409 objects -- 362 VkImageView, 41 VkImage, 3 VkPipeline, 2 VkShaderModule, 1 VkBuffer. The cap is announced in a line that reads as a formatting note, and the truncated number reads as a complete count. Always lift it: printf 'khronos_validation.duplicate_message_limit = 0\n' > scratch/vklayer/vk_layer_settings.txt and export VK_LAYER_SETTINGS_PATH to that file.

> Every result this instrument produced is suspect until it is re-validated.
