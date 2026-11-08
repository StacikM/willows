/* wdep.dll: a leaf library (imported by wlib.dll) */
#define WINAPI __attribute__((ms_abi))
int WINAPI EXPORT_DepTriple(int x){ return x * 3; }
