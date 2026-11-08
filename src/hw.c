#include "kernel.h"
/* Hardware information for programs: the graphics adapter found on the PCI bus, exposed through the D3DKMT calls (gdi32) that system-info
   tools use. Willows has no GPU driver (it draws to a framebuffer), but it can say honestly what card is installed. */
typedef unsigned short wc16;
static u32 pcr(u8 b, u8 d, u8 f, u8 o){ outl(0xCF8, 0x80000000u | ((u32)b << 16) | ((u32)d << 11) | ((u32)f << 8) | (o & 0xFC)); return inl(0xCFC); }
static struct { int done, found; u8 bus, dev, fn; u16 ven, did; char name[96]; } gpu;
static void detect_gpu(void){
  if(gpu.done) return; gpu.done = 1;
  for(int b = 0; b < 256 && !gpu.found; b++) for(int d = 0; d < 32 && !gpu.found; d++){
    u32 id0 = pcr((u8)b, (u8)d, 0, 0); if((id0 & 0xFFFF) == 0xFFFF) continue;
    int nf = (pcr((u8)b, (u8)d, 0, 0x0C) >> 16) & 0x80 ? 8 : 1;
    for(int f = 0; f < nf; f++){
      u32 id = pcr((u8)b, (u8)d, (u8)f, 0); if((id & 0xFFFF) == 0xFFFF) continue;
      u32 cls = pcr((u8)b, (u8)d, (u8)f, 8) >> 24; if(cls != 0x03) continue;          /* display controller */
      gpu.found = 1; gpu.bus = (u8)b; gpu.dev = (u8)d; gpu.fn = (u8)f; gpu.ven = (u16)(id & 0xFFFF); gpu.did = (u16)(id >> 16); break;
    }
  }
  const char *vn = "Unknown vendor"; const char *pre = 0;
  if(!gpu.found){ { const char *s = "Willows framebuffer display"; int i = 0; for(; s[i]; i++) gpu.name[i] = s[i]; gpu.name[i] = 0; } return; }
  switch(gpu.ven){ case 0x10DE: vn = "NVIDIA GPU"; break; case 0x1002: case 0x1022: vn = "AMD Radeon GPU"; break; case 0x8086: vn = "Intel Graphics"; break; case 0x1234: vn = "QEMU Standard VGA (Bochs)"; break;
    case 0x15AD: vn = "VMware SVGA II Adapter"; break; case 0x1AF4: vn = "Virtio GPU"; break; case 0x80EE: vn = "VirtualBox Graphics Adapter"; break; case 0x1414: vn = "Microsoft Basic Render Driver"; break; }
  (void)pre; char *o = gpu.name; for(const char *q = vn; *q && o < gpu.name + 60; ) *o++ = *q++;
  if(gpu.ven != 0x1234 && gpu.ven != 0x15AD && gpu.ven != 0x1AF4 && gpu.ven != 0x80EE){
    const char *hx = "0123456789ABCDEF"; *o++ = ' '; *o++ = '['; *o++ = hx[gpu.ven >> 12]; *o++ = hx[(gpu.ven >> 8) & 15]; *o++ = hx[(gpu.ven >> 4) & 15]; *o++ = hx[gpu.ven & 15]; *o++ = ':';
    *o++ = hx[gpu.did >> 12]; *o++ = hx[(gpu.did >> 8) & 15]; *o++ = hx[(gpu.did >> 4) & 15]; *o++ = hx[gpu.did & 15]; *o++ = ']';
  }
  *o = 0;
}
static void wset(wc16 *w, const char *s, int max){ int i = 0; for(; s[i] && i < max - 1; i++) w[i] = (u8)s[i]; w[i] = 0; }
#define STATUS_OK 0
#define STATUS_NOT_SUPPORTED ((i32)0xC00000BB)

/* D3DKMT_ENUMADAPTERS: UINT NumAdapters; D3DKMT_ADAPTERINFO[16] { UINT hAdapter; LUID(8); ULONG NumOfSources; BOOL present }  (20 bytes each) */
static i32 WINAPI k_D3DKMTEnumAdapters(u8 *p){
  detect_gpu(); memset(p, 0, 4 + 16 * 20); *(u32 *)p = 1; *(u32 *)(p + 4) = 1; *(u32 *)(p + 8) = 1; *(i32 *)(p + 12) = 0; *(u32 *)(p + 16) = 1; return STATUS_OK;
}
static i32 WINAPI k_D3DKMTCloseAdapter(u8 *p){ (void)p; return STATUS_OK; }
/* D3DKMT_QUERYADAPTERINFO: UINT hAdapter; UINT Type; void *pData; UINT size */
static i32 WINAPI k_D3DKMTQueryAdapterInfo(u8 *q){
  detect_gpu(); u32 type = *(u32 *)(q + 4); u8 *data = *(u8 **)(q + 8); u32 size = *(u32 *)(q + 16);
  switch(type){
    case 4: if(size >= 12){ *(u32 *)data = gpu.bus; *(u32 *)(data + 4) = gpu.dev; *(u32 *)(data + 8) = gpu.fn; return STATUS_OK; } break;           /* ADAPTERADDRESS */
    case 8: if(size >= 260 * 2 * 4){ memset(data, 0, 260 * 2 * 4); wset((wc16 *)data, gpu.name, 260); wset((wc16 *)(data + 520), "Willows", 260); wset((wc16 *)(data + 1040), "Internal", 260); wset((wc16 *)(data + 1560), gpu.name, 260); return STATUS_OK; } break;   /* ADAPTERREGISTRYINFO */
    case 13: if(size >= 8){ *(u64 *)data = 0x0001000000000000ull; return STATUS_OK; } break;                                                              /* DRIVERVERSION */
    case 15: if(size >= 4){ *(u32 *)data = 0x1 | 0x2 | 0x8; return STATUS_OK; } break;                                                                         /* ADAPTERTYPE: render + display */
  }
  return STATUS_NOT_SUPPORTED;
}
static i32 WINAPI k_D3DKMTQueryStatistics(void *p){ (void)p; return STATUS_NOT_SUPPORTED; }
/* for neofetch-style tools inside Willows: the adapter name */
const char *hw_gpu_name(void){ detect_gpu(); return gpu.name; }
#define H(n) {"gdi32", #n, k_##n}
const struct export hw_exports[] = { H(D3DKMTEnumAdapters), H(D3DKMTCloseAdapter), H(D3DKMTQueryAdapterInfo), H(D3DKMTQueryStatistics) };
const unsigned hw_nexports = sizeof hw_exports / sizeof *hw_exports;
