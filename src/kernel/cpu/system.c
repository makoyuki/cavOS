#include <console.h>
#include <system.h>

// Source code for handling ports via assembly references
// Copyright (C) 2024 Panagiotis

uint8_t inportb(uint16_t _port) {
  uint8_t rv;
  __asm__ __volatile__("inb %1, %0" : "=a"(rv) : "dN"(_port));
  return rv;
}

void outportb(uint16_t _port, uint8_t _data) {
  __asm__ __volatile__("outb %1, %0" : : "dN"(_port), "a"(_data));
}

uint16_t inportw(uint16_t port) {
  uint16_t result;
  __asm__("in %%dx, %%ax" : "=a"(result) : "d"(port));
  return result;
}

void outportw(unsigned short port, unsigned short data) {
  __asm__("out %%ax, %%dx" : : "a"(data), "d"(port));
}

uint32_t inportl(uint16_t portid) {
  uint32_t ret;
  __asm__ __volatile__("inl %%dx, %%eax" : "=a"(ret) : "d"(portid));
  return ret;
}

void outportl(uint16_t portid, uint32_t value) {
  __asm__ __volatile__("outl %%eax, %%dx" : : "d"(portid), "a"(value));
}

uint64_t rdmsr(uint32_t msrid) {
  uint32_t low;
  uint32_t high;
  __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msrid));
  return (uint64_t)low << 0 | (uint64_t)high << 32;
}

uint64_t wrmsr(uint32_t msrid, uint64_t value) {
  uint32_t low = value >> 0 & 0xFFFFFFFF;
  uint32_t high = value >> 32 & 0xFFFFFFFF;
  __asm__ __volatile__("wrmsr" : : "a"(low), "d"(high), "c"(msrid) : "memory");
  return value;
}

void panic() {
  debugf("[kernel] Kernel panic triggered!\n");
  asm volatile("cli");
  asm volatile("hlt");
}

bool checkInterrupts() {
  uint16_t flags;
  asm volatile("pushf; pop %0" : "=g"(flags));
  return flags & (1 << 9);
}

bool interruptStatus = true;

void lockInterrupts() {
  interruptStatus = checkInterrupts();
  asm volatile("cli");
}

void releaseInterrupts() {
  if (interruptStatus)
    asm volatile("sti");
  else
    interruptStatus = !interruptStatus;
}

// Endianness
uint16_t switch_endian_16(uint16_t val) { return (val << 8) | (val >> 8); }

uint32_t switch_endian_32(uint32_t val) {
  return (val << 24) | ((val << 8) & 0x00FF0000) | ((val >> 8) & 0x0000FF00) |
         (val >> 24);
}
