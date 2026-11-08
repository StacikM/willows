/* wdyn.dll: loaded at run time with LoadLibrary */
#define WINAPI __attribute__((ms_abi))
int WINAPI EXPORT_Mul(int a, int b){ return a * b; }
