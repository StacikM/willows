#include "kernel.h"
/* Security objects (advapi32): Willows has one user and enforces no access checks, but programs build and inspect SIDs, ACLs and
   security descriptors (Firefox's sandbox and its read-only shared memory, installers, Chromium-style code). These functions produce
   real binary structures with the Windows layouts so that code walking them works:
     SID   = revision 1, count, 6-byte authority (big-endian), count x u32 sub-authorities
     ACL   = revision, 0, size u16, ace count u16, 0 u16; then ACEs {type, flags, size u16, mask u32, SID}
     SECURITY_DESCRIPTOR (absolute) = revision 1, 0, control u16, then owner, group, sacl, dacl pointers
   The access token belongs to user S-1-5-21-1000000000-2000000000-3000000000-1000, medium integrity, not elevated. */
typedef u32 DWORD; typedef i32 BOOL; typedef u64 HANDLE; typedef u16 wc;
#define ERR(e) do{ win32_set_last_error(e); return 0; }while(0)

static u32 sid_len(const u8 *s){ return 8+4u*s[1]; }
static int sid_ok(const u8 *s){ return s && s[0]==1 && s[1]<=15; }
static u32 make_sid(u8 *out, u64 auth, int n, const u32 *sub){
  out[0]=1; out[1]=(u8)n; for(int i=0;i<6;i++) out[2+i]=(u8)(auth>>(8*(5-i)));
  for(int i=0;i<n;i++) ((u32*)(out+8))[i]=sub[i]; return 8+4u*n;
}
static const u32 user_sub[5]={21,1000000000u,2000000000u,3000000000u,1000};
static u32 user_sid(u8 *o){ return make_sid(o,5,5,user_sub); }
static u32 world_sid(u8 *o){ u32 z=0; return make_sid(o,1,1,&z); }
static u32 medium_label(u8 *o){ u32 v=0x2000; return make_sid(o,16,1,&v); }

/* ---- SIDs ---- */
static BOOL WINAPI s_IsValidSid(const u8 *s){ return sid_ok(s); }
static DWORD WINAPI s_GetLengthSid(const u8 *s){ return sid_ok(s)? sid_len(s) : 0; }
static BOOL WINAPI s_CopySid(DWORD n, u8 *d, const u8 *s){ if(!sid_ok(s)) ERR(87); if(n<sid_len(s)) ERR(122); memcpy(d,s,sid_len(s)); return 1; }
static BOOL WINAPI s_EqualSid(const u8 *a, const u8 *b){ if(!sid_ok(a)||!sid_ok(b)) return 0; return sid_len(a)==sid_len(b) && !memcmp(a,b,sid_len(a)); }
static BOOL WINAPI s_EqualPrefixSid(const u8 *a, const u8 *b){ if(!sid_ok(a)||!sid_ok(b)||a[1]!=b[1]||!a[1]) return 0; return !memcmp(a,b,8+4u*(a[1]-1)); }
static BOOL WINAPI s_InitializeSid(u8 *s, const u8 *auth, u8 n){ if(n>15) ERR(87); s[0]=1; s[1]=n; memcpy(s+2,auth,6); memset(s+8,0,4u*n); return 1; }
static u32 *WINAPI s_GetSidSubAuthority(u8 *s, DWORD i){ return (u32*)(s+8)+i; }
static u8 *WINAPI s_GetSidSubAuthorityCount(u8 *s){ return s+1; }
static u8 *WINAPI s_GetSidIdentifierAuthority(u8 *s){ return s+2; }
static DWORD WINAPI s_GetSidLengthRequired(u8 n){ return 8+4u*n; }
static BOOL WINAPI s_AllocateAndInitializeSid(const u8 *auth, u8 n, DWORD a0, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7, u8 **out){
  if(n>8 || !out) ERR(87); u32 sub[8]={a0,a1,a2,a3,a4,a5,a6,a7}; u8 *s=heap_alloc(8+4u*n); if(!s) ERR(8);
  s[0]=1; s[1]=n; memcpy(s+2,auth,6); memcpy(s+8,sub,4u*n); *out=s; return 1;
}
static void *WINAPI s_FreeSid(void *s){ heap_free(s); return 0; }
/* CreateWellKnownSid: the common ones */
static BOOL WINAPI s_CreateWellKnownSid(int type, const u8 *dom, u8 *out, DWORD *size){
  (void)dom; u8 t[68]; u32 n=0, v;
  switch(type){
    case 0: v=0; n=make_sid(t,0,1,&v); break;                      /* WinNullSid S-1-0-0 */
    case 1: n=world_sid(t); break;                                  /* WinWorldSid S-1-1-0 */
    case 2: v=0; n=make_sid(t,2,1,&v); break;                      /* WinLocalSid S-1-2-0 */
    case 3: v=0; n=make_sid(t,3,1,&v); break;                      /* WinCreatorOwnerSid S-1-3-0 */
    case 4: v=4; n=make_sid(t,5,1,&v); break;                      /* WinInteractiveSid S-1-5-4 */
    case 11: v=12; n=make_sid(t,5,1,&v); break;                    /* WinRestrictedCodeSid S-1-5-12 */
    case 17: v=11; n=make_sid(t,5,1,&v); break;                    /* WinAuthenticatedUserSid S-1-5-11 */
    case 22: v=18; n=make_sid(t,5,1,&v); break;                    /* WinLocalSystemSid S-1-5-18 */
    case 26: { u32 s2[2]={32,544}; n=make_sid(t,5,2,s2); break; }   /* WinBuiltinAdministratorsSid */
    case 27: { u32 s2[2]={32,545}; n=make_sid(t,5,2,s2); break; }   /* WinBuiltinUsersSid */
    case 65: v=0; n=make_sid(t,16,1,&v); break;                    /* WinUntrustedLabelSid */
    case 66: v=0x1000; n=make_sid(t,16,1,&v); break;               /* WinLowLabelSid */
    case 67: n=medium_label(t); break;                              /* WinMediumLabelSid */
    case 68: v=0x3000; n=make_sid(t,16,1,&v); break;               /* WinHighLabelSid */
    case 69: v=0x4000; n=make_sid(t,16,1,&v); break;               /* WinSystemLabelSid */
    case 84: { u32 s2[2]={2,1}; n=make_sid(t,15,2,s2); break; }    /* WinBuiltinAnyPackageSid S-1-15-2-1 */
    default: ERR(87);
  }
  if(!size) ERR(87); if(!out || *size<n){ *size=n; ERR(122); } memcpy(out,t,n); *size=n; return 1;
}
static int sid_to_str(const u8 *s, char *o, int max){
  u64 auth=0; for(int i=0;i<6;i++) auth=(auth<<8)|s[2+i];
  int k=ksnprintf(o,(u32)max,"S-1-%u",(u32)auth); for(int i=0;i<s[1] && k<max-12;i++) k+=ksnprintf(o+k,(u32)(max-k),"-%u",((const u32*)(s+8))[i]); return k;
}
static BOOL WINAPI s_ConvertSidToStringSidA(const u8 *s, char **out){ if(!sid_ok(s)||!out) ERR(1337); char t[200]; int n=sid_to_str(s,t,sizeof t); char *r=heap_alloc(n+1); memcpy(r,t,n+1); *out=r; return 1; }
static BOOL WINAPI s_ConvertSidToStringSidW(const u8 *s, wc **out){ if(!sid_ok(s)||!out) ERR(1337); char t[200]; int n=sid_to_str(s,t,sizeof t); wc *r=heap_alloc((n+1)*2); for(int i=0;i<=n;i++) r[i]=(u8)t[i]; *out=r; return 1; }
static BOOL str_to_sid(const char *p, u8 **out){
  if((p[0]|32)!='s' || p[1]!='-' || p[2]!='1' || p[3]!='-') ERR(1337); p+=4;
  u64 nums[17]; int n=0; while(*p && n<17){ u64 v=0; if(*p<'0'||*p>'9') ERR(1337); while(*p>='0'&&*p<='9') v=v*10+(u64)(*p++-'0'); nums[n++]=v; if(*p=='-') p++; else break; }
  if(n<1) ERR(1337); u32 sub[16]; for(int i=1;i<n;i++) sub[i-1]=(u32)nums[i];
  u8 *s=heap_alloc(8+4u*(n-1)); make_sid(s,nums[0],n-1,sub); *out=s; return 1;
}
static BOOL WINAPI s_ConvertStringSidToSidA(const char *str, u8 **out){ return str_to_sid(str,out); }
static BOOL WINAPI s_ConvertStringSidToSidW(const wc *str, u8 **out){ char t[200]; int i=0; for(;str[i]&&i<199;i++) t[i]=(char)str[i]; t[i]=0; return str_to_sid(t,out); }

/* ---- ACLs ---- */
static BOOL WINAPI s_InitializeAcl(u8 *acl, DWORD len, DWORD rev){ if(len<8 || len>0xFFFF) ERR(87); memset(acl,0,8); acl[0]=(u8)rev; *(u16*)(acl+2)=(u16)len; return 1; }
static BOOL WINAPI s_IsValidAcl(const u8 *acl){ return acl && (acl[0]==2||acl[0]==4); }
static u8 *ace_end(u8 *acl){ u8 *p=acl+8; for(u16 i=0;i<*(u16*)(acl+4);i++) p+=*(u16*)(p+2); return p; }
static BOOL add_ace(u8 *acl, u8 type, u8 flags, DWORD mask, const u8 *sid){
  if(!acl || !sid_ok(sid)) ERR(87);
  u8 *p=ace_end(acl); u32 sz=8+sid_len(sid); if((u32)(p-acl)+sz>*(u16*)(acl+2)) ERR(1344);   /* ERROR_ALLOTTED_SPACE_EXCEEDED */
  p[0]=type; p[1]=flags; *(u16*)(p+2)=(u16)sz; *(u32*)(p+4)=mask; memcpy(p+8,sid,sid_len(sid)); (*(u16*)(acl+4))++; return 1;
}
static BOOL WINAPI s_AddAccessAllowedAce(u8 *acl, DWORD rev, DWORD mask, const u8 *sid){ (void)rev; return add_ace(acl,0,0,mask,sid); }
static BOOL WINAPI s_AddAccessAllowedAceEx(u8 *acl, DWORD rev, DWORD fl, DWORD mask, const u8 *sid){ (void)rev; return add_ace(acl,0,(u8)fl,mask,sid); }
static BOOL WINAPI s_AddAccessDeniedAce(u8 *acl, DWORD rev, DWORD mask, const u8 *sid){ (void)rev; return add_ace(acl,1,0,mask,sid); }
static BOOL WINAPI s_AddAccessDeniedAceEx(u8 *acl, DWORD rev, DWORD fl, DWORD mask, const u8 *sid){ (void)rev; return add_ace(acl,1,(u8)fl,mask,sid); }
static BOOL WINAPI s_AddMandatoryAce(u8 *acl, DWORD rev, DWORD fl, DWORD policy, const u8 *sid){ (void)rev; return add_ace(acl,0x11,(u8)fl,policy,sid); }
static BOOL WINAPI s_GetAce(u8 *acl, DWORD i, u8 **ace){ if(!acl || i>=*(u16*)(acl+4)) ERR(87); u8 *p=acl+8; for(DWORD k=0;k<i;k++) p+=*(u16*)(p+2); *ace=p; return 1; }
static BOOL WINAPI s_GetAclInformation(const u8 *acl, u32 *info, DWORD len, int cls){
  if(cls==1 && len>=4){ info[0]=acl[0]; return 1; }                                     /* AclRevisionInformation */
  if(cls==2 && len>=12){ u8 *e=ace_end((u8*)acl); info[0]=*(u16*)(acl+4); info[1]=(u32)(e-acl); info[2]=*(u16*)(acl+2)-(u32)(e-acl); return 1; }   /* AclSizeInformation */
  ERR(87);
}
static u8 *new_acl_world(void){ u8 *a=heap_alloc(64); s_InitializeAcl(a,64,2); u8 w[16]; world_sid(w); add_ace(a,0,0,0x10000000,w); return a; }   /* Everyone: GENERIC_ALL */
/* SetEntriesInAcl: EXPLICIT_ACCESS = {permissions, mode, inheritance, TRUSTEE{multiple ptr, op, form, type, name ptr}} (48 bytes) */
static DWORD WINAPI s_SetEntriesInAclW(DWORD n, const u8 *ea, const u8 *old, u8 **out){
  u32 sz=8; if(old) sz+=*(u16*)(old+2); sz+=n*(8+68); u8 *a=heap_alloc(sz); if(!a) return 8; s_InitializeAcl(a,sz,2);
  if(old){ u8 *p=(u8*)old+8; for(u16 i=0;i<*(u16*)(old+4);i++){ add_ace(a,p[0],p[1],*(u32*)(p+4),p+8); p+=*(u16*)(p+2); } }
  for(DWORD i=0;i<n;i++){ const u8 *e=ea+48*i; u32 perm=*(const u32*)e, mode=*(const u32*)(e+4), inh=*(const u32*)(e+8), form=*(const u32*)(e+28);
    u8 w[16]; const u8 *sid= form==0? *(u8*const*)(e+40) : (world_sid(w), w);                    /* TRUSTEE_IS_SID, otherwise Everyone */
    if(mode==1||mode==2) add_ace(a,0,(u8)inh,perm,sid); else if(mode==3) add_ace(a,1,(u8)inh,perm,sid); }   /* GRANT/SET_ACCESS, DENY_ACCESS */
  *out=a; return 0;
}
static DWORD WINAPI s_SetEntriesInAclA(DWORD n, const u8 *ea, const u8 *old, u8 **out){ return s_SetEntriesInAclW(n,ea,old,out); }

/* TRUSTEE = {pMultipleTrustee, MultipleTrusteeOperation, TrusteeForm, TrusteeType, ptstrName} (32 bytes) */
static void WINAPI s_BuildTrusteeWithSidW(u8 *t, u8 *sid){ memset(t,0,32); *(u32*)(t+12)=0; *(u32*)(t+16)=0; *(u8**)(t+24)=sid; }      /* TRUSTEE_IS_SID */
static void WINAPI s_BuildTrusteeWithNameW(u8 *t, wc *name){ memset(t,0,32); *(u32*)(t+12)=1; *(wc**)(t+24)=name; }                   /* TRUSTEE_IS_NAME */
static DWORD WINAPI s_GetEffectiveRightsFromAclW(const u8 *acl, const u8 *t, DWORD *mask){ (void)acl;(void)t; *mask=0x1F01FF; return 0; }  /* nothing is enforced: all rights */

/* ---- security descriptors (absolute: 40 bytes) ---- */
static BOOL WINAPI s_InitializeSecurityDescriptor(u8 *sd, DWORD rev){ memset(sd,0,40); sd[0]=(u8)rev; return 1; }
static BOOL WINAPI s_IsValidSecurityDescriptor(const u8 *sd){ return sd && sd[0]==1; }
static BOOL WINAPI s_SetSecurityDescriptorDacl(u8 *sd, BOOL present, u8 *acl, BOOL def){ u16 *c=(u16*)(sd+2); *c=(u16)((*c&~0x0Cu)|(present? 4 : 0)|(def? 8 : 0)); *(u8**)(sd+32)= present? acl : 0; return 1; }
static BOOL WINAPI s_SetSecurityDescriptorSacl(u8 *sd, BOOL present, u8 *acl, BOOL def){ u16 *c=(u16*)(sd+2); *c=(u16)((*c&~0x30u)|(present? 0x10 : 0)|(def? 0x20 : 0)); *(u8**)(sd+24)= present? acl : 0; return 1; }
static BOOL WINAPI s_SetSecurityDescriptorOwner(u8 *sd, u8 *sid, BOOL def){ *(u8**)(sd+8)=sid; if(def) *(u16*)(sd+2)|=1; return 1; }
static BOOL WINAPI s_SetSecurityDescriptorGroup(u8 *sd, u8 *sid, BOOL def){ *(u8**)(sd+16)=sid; if(def) *(u16*)(sd+2)|=2; return 1; }
static BOOL WINAPI s_SetSecurityDescriptorControl(u8 *sd, u16 mask, u16 bits){ u16 *c=(u16*)(sd+2); *c=(u16)((*c&~mask)|(bits&mask)); return 1; }
static BOOL WINAPI s_GetSecurityDescriptorDacl(const u8 *sd, BOOL *present, u8 **acl, BOOL *def){ u16 c=*(const u16*)(sd+2); *present=(c&4)!=0; *acl=*(u8*const*)(sd+32); if(def) *def=(c&8)!=0; return 1; }
static BOOL WINAPI s_GetSecurityDescriptorSacl(const u8 *sd, BOOL *present, u8 **acl, BOOL *def){ u16 c=*(const u16*)(sd+2); *present=(c&0x10)!=0; *acl=*(u8*const*)(sd+24); if(def) *def=(c&0x20)!=0; return 1; }
static BOOL WINAPI s_GetSecurityDescriptorOwner(const u8 *sd, u8 **sid, BOOL *def){ *sid=*(u8*const*)(sd+8); if(def) *def=0; return 1; }
static BOOL WINAPI s_GetSecurityDescriptorGroup(const u8 *sd, u8 **sid, BOOL *def){ *sid=*(u8*const*)(sd+16); if(def) *def=0; return 1; }
static BOOL WINAPI s_GetSecurityDescriptorControl(const u8 *sd, u16 *c, DWORD *rev){ *c=*(const u16*)(sd+2); if(rev) *rev=sd[0]; return 1; }
static DWORD WINAPI s_GetSecurityDescriptorLength(const u8 *sd){ (void)sd; return 40; }
/* one allocation holding a descriptor with owner = group = the user, and a DACL "Everyone: all access" (LocalFree frees it) */
static u8 *object_sd(void){
  u8 *b=heap_alloc(256); if(!b) return 0; s_InitializeSecurityDescriptor(b,1);
  u8 *sid=b+40; user_sid(sid); u8 *acl=b+96; s_InitializeAcl(acl,64,2); u8 w[16]; world_sid(w); add_ace(acl,0,0,0x1F0FFF|0x10000000,w);
  s_SetSecurityDescriptorOwner(b,sid,0); s_SetSecurityDescriptorGroup(b,sid,0); s_SetSecurityDescriptorDacl(b,1,acl,0); return b;
}
static DWORD WINAPI s_GetSecurityInfo(HANDLE h, int type, DWORD info, u8 **owner, u8 **group, u8 **dacl, u8 **sacl, u8 **sd){
  (void)h;(void)type;(void)info; u8 *b=object_sd(); if(!b) return 8;
  if(owner) *owner=*(u8**)(b+8); if(group) *group=*(u8**)(b+16); if(dacl) *dacl=*(u8**)(b+32); if(sacl) *sacl=0;
  if(sd) *sd=b; return 0;                                                               /* (without sd the block is kept: tiny) */
}
static DWORD WINAPI s_GetNamedSecurityInfoW(const wc *n, int type, DWORD info, u8 **owner, u8 **group, u8 **dacl, u8 **sacl, u8 **sd){ (void)n; return s_GetSecurityInfo(0,type,info,owner,group,dacl,sacl,sd); }
static DWORD WINAPI s_SetSecurityInfo(HANDLE h, int type, DWORD info, void *o, void *g, void *d, void *s){ (void)h;(void)type;(void)info;(void)o;(void)g;(void)d;(void)s; return 0; }
static DWORD WINAPI s_SetNamedSecurityInfoW(wc *n, int type, DWORD info, void *o, void *g, void *d, void *s){ (void)n; return s_SetSecurityInfo(0,type,info,o,g,d,s); }
static BOOL WINAPI s_GetKernelObjectSecurity(HANDLE h, DWORD info, u8 *sd, DWORD n, DWORD *need){ (void)h;(void)info; if(need) *need=256; if(n<256){ win32_set_last_error(122); return 0; } u8 *b=object_sd(); memcpy(sd,b,256); heap_free(b);
  *(u8**)(sd+8)=sd+40; *(u8**)(sd+16)=sd+40; *(u8**)(sd+32)=sd+96; return 1; }
static BOOL WINAPI s_SetKernelObjectSecurity(HANDLE h, DWORD info, const void *sd){ (void)h;(void)info;(void)sd; return 1; }
static BOOL WINAPI s_ConvertStringSecurityDescriptorToSecurityDescriptorW(const wc *s, DWORD rev, u8 **sd, DWORD *len){ (void)s;(void)rev; u8 *b=object_sd(); if(!b) ERR(8); *sd=b; if(len) *len=256; return 1; }
static BOOL WINAPI s_ConvertStringSecurityDescriptorToSecurityDescriptorA(const char *s, DWORD rev, u8 **sd, DWORD *len){ (void)s; return s_ConvertStringSecurityDescriptorToSecurityDescriptorW(0,rev,sd,len); }

/* ---- the access token ---- */
#define TOKEN_H 0x7D00
static BOOL WINAPI s_OpenProcessToken(HANDLE p, DWORD acc, HANDLE *t){ (void)p;(void)acc; *t=TOKEN_H; return 1; }
static BOOL WINAPI s_OpenThreadToken(HANDLE th, DWORD acc, BOOL self, HANDLE *t){ (void)th;(void)acc;(void)self; *t=0; ERR(1008); }   /* ERROR_NO_TOKEN: not impersonating */
static HANDLE WINAPI s_GetCurrentProcessToken(void){ return (HANDLE)-4; }
static BOOL WINAPI s_DuplicateTokenEx(HANDLE t, DWORD acc, void *sa, int lvl, int type, HANDLE *out){ (void)t;(void)acc;(void)sa;(void)lvl;(void)type; *out=TOKEN_H; return 1; }
static BOOL WINAPI s_DuplicateToken(HANDLE t, int lvl, HANDLE *out){ (void)t;(void)lvl; *out=TOKEN_H; return 1; }
static BOOL WINAPI s_CreateRestrictedToken(HANDLE t, DWORD fl, DWORD nd, void *dis, DWORD np, void *privs, DWORD nr, void *res, HANDLE *out){   /* restrictions are not enforced */
  (void)t;(void)fl;(void)nd;(void)dis;(void)np;(void)privs;(void)nr;(void)res; *out=TOKEN_H; return 1;
}
static BOOL WINAPI s_CheckTokenMembership(HANDLE t, const u8 *sid, BOOL *is){ (void)t; u8 w[16]; world_sid(w); u8 u[32]; user_sid(u); *is= s_EqualSid(sid,w) || s_EqualSid(sid,u); return 1; }
static BOOL WINAPI s_GetTokenInformation(HANDLE t, int cls, u8 *out, DWORD n, DWORD *ret){
  (void)t; u8 b[160]; u32 sz=0; memset(b,0,sizeof b);
  switch(cls){
    case 1: sz=16+user_sid(b+16); *(u8**)b=out+16; break;                               /* TokenUser: SID_AND_ATTRIBUTES + SID */
    case 2: *(u32*)b=1; sz=24+world_sid(b+24); *(u8**)(b+8)=out+24; *(u32*)(b+16)=7; break;   /* TokenGroups: Everyone, enabled */
    case 3: sz=4; break;                                                                 /* TokenPrivileges: none */
    case 4: case 5: sz=8+user_sid(b+8); *(u8**)b=out+8; break;                          /* TokenOwner, TokenPrimaryGroup */
    case 6: sz=8+64; *(u8**)b=out+8; { u8 *a=b+8; s_InitializeAcl(a,64,2); u8 w[16]; world_sid(w); add_ace(a,0,0,0x10000000,w); } break;   /* TokenDefaultDacl */
    case 8: *(u32*)b=1; sz=4; break;                                                     /* TokenType: primary */
    case 9: sz=4; break;                                                                 /* TokenImpersonationLevel */
    case 10: sz=56; *(u32*)(b+24)=1; break;                                              /* TokenStatistics (TokenType at 24) */
    case 12: *(u32*)b=1; sz=4; break;                                                    /* TokenSessionId */
    case 18: *(u32*)b=1; sz=4; break;                                                    /* TokenElevationType: default */
    case 20: sz=4; break;                                                                /* TokenElevation: not elevated */
    case 25: sz=16+medium_label(b+16); *(u8**)b=out+16; *(u32*)(b+8)=0x60; break;       /* TokenIntegrityLevel: medium */
    case 29: sz=4; break;                                                                /* TokenIsAppContainer: no */
    case 31: sz=4; break;                                                                /* TokenAppContainerNumber */
    default: sz=4; break;
  }
  if(ret) *ret=sz; if(!out || n<sz) ERR(122); memcpy(out,b,sz); return 1;
}
static BOOL WINAPI s_SetTokenInformation(HANDLE t, int cls, void *i, DWORD n){ (void)t;(void)cls;(void)i;(void)n; return 1; }
static BOOL WINAPI s_AdjustTokenPrivileges(HANDLE t, BOOL dis, void *ns, DWORD n, void *prev, DWORD *ret){ (void)t;(void)dis;(void)ns;(void)n;(void)prev; if(ret) *ret=0; win32_set_last_error(0); return 1; }
static BOOL WINAPI s_LookupPrivilegeValueW(const wc *sys, const wc *name, u64 *luid){ (void)sys;(void)name; *luid=20; return 1; }
static BOOL WINAPI s_ImpersonateSelf(int lvl){ (void)lvl; return 1; }
static BOOL WINAPI s_RevertToSelf(void){ return 1; }

#define S(n) {"advapi32",#n,s_##n}
const struct export sec_exports[] = {
  S(IsValidSid), S(GetLengthSid), S(CopySid), S(EqualSid), S(EqualPrefixSid), S(InitializeSid), S(GetSidSubAuthority), S(GetSidSubAuthorityCount), S(GetSidIdentifierAuthority),
  S(GetSidLengthRequired), S(AllocateAndInitializeSid), S(FreeSid), S(CreateWellKnownSid), S(ConvertSidToStringSidA), S(ConvertSidToStringSidW), S(ConvertStringSidToSidA), S(ConvertStringSidToSidW),
  S(InitializeAcl), S(IsValidAcl), S(AddAccessAllowedAce), S(AddAccessAllowedAceEx), S(AddAccessDeniedAce), S(AddAccessDeniedAceEx), S(AddMandatoryAce), S(GetAce), S(GetAclInformation),
  S(SetEntriesInAclW), S(SetEntriesInAclA), S(BuildTrusteeWithSidW), {"advapi32","BuildTrusteeWithSidA",s_BuildTrusteeWithSidW}, S(BuildTrusteeWithNameW), S(GetEffectiveRightsFromAclW),
  S(InitializeSecurityDescriptor), S(IsValidSecurityDescriptor), S(SetSecurityDescriptorDacl), S(SetSecurityDescriptorSacl), S(SetSecurityDescriptorOwner), S(SetSecurityDescriptorGroup),
  S(SetSecurityDescriptorControl), S(GetSecurityDescriptorDacl), S(GetSecurityDescriptorSacl), S(GetSecurityDescriptorOwner), S(GetSecurityDescriptorGroup), S(GetSecurityDescriptorControl),
  S(GetSecurityDescriptorLength), S(GetSecurityInfo), S(GetNamedSecurityInfoW), S(SetSecurityInfo), S(SetNamedSecurityInfoW), S(GetKernelObjectSecurity), S(SetKernelObjectSecurity),
  S(ConvertStringSecurityDescriptorToSecurityDescriptorW), S(ConvertStringSecurityDescriptorToSecurityDescriptorA),
  S(OpenProcessToken), S(OpenThreadToken), S(GetCurrentProcessToken), {"kernel32","GetCurrentProcessToken",s_GetCurrentProcessToken}, S(DuplicateTokenEx), S(DuplicateToken), S(CreateRestrictedToken),
  S(CheckTokenMembership), S(GetTokenInformation), S(SetTokenInformation), S(AdjustTokenPrivileges), S(LookupPrivilegeValueW), {"advapi32","LookupPrivilegeValueA",s_LookupPrivilegeValueW},
  S(ImpersonateSelf), S(RevertToSelf),
};
const unsigned sec_nexports = sizeof sec_exports / sizeof *sec_exports;
