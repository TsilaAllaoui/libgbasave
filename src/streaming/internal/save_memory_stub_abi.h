#pragma once

#include <cstdint>

namespace gbasave::save_memory_stub_abi {

// sfw_save_stubs.bin ABI frozen from the hardware-proven GBABR runtime.
inline constexpr std::uint32_t kEepromReadOff = 0x00u;
inline constexpr std::uint32_t kEepromReadLen = 0x20u;
inline constexpr std::uint32_t kEepromWriteOff = 0x20u;
inline constexpr std::uint32_t kEepromWriteLen = 0x20u;
inline constexpr std::uint32_t kFlashReadOff = 0x40u;
inline constexpr std::uint32_t kFlashReadLen = 0x20u;
inline constexpr std::uint32_t kFlashReadDispatch = 0x1Cu;
inline constexpr std::uint32_t kFlashEraseChipOff = 0x60u;
inline constexpr std::uint32_t kFlashEraseChipLen = 0x18u;
inline constexpr std::uint32_t kFlashEraseChipDispatch = 0x14u;
inline constexpr std::uint32_t kFlashEraseSectorOff = 0x78u;
inline constexpr std::uint32_t kFlashEraseSectorLen = 0x1Cu;
inline constexpr std::uint32_t kFlashEraseSectorDispatch = 0x18u;
inline constexpr std::uint32_t kFlashWriteSectorOff = 0x94u;
inline constexpr std::uint32_t kFlashWriteSectorLen = 0x20u;
inline constexpr std::uint32_t kFlashWriteSectorDispatch = 0x1Cu;
inline constexpr std::uint32_t kFlashWriteByteOff = 0xB4u;
inline constexpr std::uint32_t kFlashWriteByteLen = 0x1Cu;
inline constexpr std::uint32_t kFlashWriteByteDispatch = 0x18u;
inline constexpr std::uint32_t kFlashIdent512Off = 0xD0u;
inline constexpr std::uint32_t kFlashIdent512Len = 0x08u;
inline constexpr std::uint32_t kFlashIdent1MOff = 0xD8u;
inline constexpr std::uint32_t kFlashIdent1MLen = 0x08u;
inline constexpr std::uint32_t kThumbRet0Off = 0xE0u;
inline constexpr std::uint32_t kThumbRet0Len = 0x04u;

// fram_flash512_inline.bin ABI.
inline constexpr std::uint32_t kF512ReadOff = 0x00u;
inline constexpr std::uint32_t kF512ReadLen = 0x20u;
inline constexpr std::uint32_t kF512ReadWindow = 0x1Cu;
inline constexpr std::uint32_t kF512EraseChipOff = 0x20u;
inline constexpr std::uint32_t kF512EraseChipLen = 0x1Cu;
inline constexpr std::uint32_t kF512EraseChipWindow = 0x18u;
inline constexpr std::uint32_t kF512EraseSectorOff = 0x3Cu;
inline constexpr std::uint32_t kF512EraseSectorLen = 0x24u;
inline constexpr std::uint32_t kF512EraseSectorWindow = 0x20u;
inline constexpr std::uint32_t kF512WriteSectorOff = 0x60u;
inline constexpr std::uint32_t kF512WriteSectorLen = 0x2Cu;
inline constexpr std::uint32_t kF512WriteSectorWindow = 0x28u;
inline constexpr std::uint32_t kF512WriteByteOff = 0x8Cu;
inline constexpr std::uint32_t kF512WriteByteLen = 0x14u;
inline constexpr std::uint32_t kF512WriteByteWindow = 0x10u;

// fram_banked_runtime.bin ABI.
inline constexpr std::uint32_t kFramDispatchOff = 0x30u;
inline constexpr std::uint32_t kFramCfgWindow = 0x0Cu;
inline constexpr std::uint32_t kFramCfgSelector = 0x10u;
inline constexpr std::uint32_t kFramCfgSelectorMask = 0x14u;
inline constexpr std::uint32_t kFramCfgSelectorShift = 0x18u;
inline constexpr std::uint32_t kFramCfgSelectorFixed = 0x1Cu;
inline constexpr std::uint32_t kFramCfgSelectorWidth = 0x20u;
inline constexpr std::uint32_t kFramCfgSectorCount = 0x24u;
inline constexpr std::uint32_t kFramCfgWindowBytes = 0x28u;
inline constexpr std::uint32_t kFramCfgBankCount = 0x2Cu;

} // namespace gbasave::save_memory_stub_abi
