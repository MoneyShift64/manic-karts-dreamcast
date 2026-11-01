/* unkart.c - unpacks Manic Karts KART.DAT (LZHUF, N=1024 F=24, XOR-scrambled table) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#define N 1024
#define F 24
#define TH 2
#define NCH (256-TH+F)
#define T (NCH*2-1)
#define R (T-1)
#define MAXF 0x8000
static const uint8_t *dat; static size_t datlen, dp; static unsigned bitbuf, bitlen;
static unsigned freq[T+1]; static int prnt[T+NCH], son[T];
static uint8_t dcode[256], dlen[256];
static int getbit(void){ if(!bitlen){bitbuf=dp<datlen?dat[dp]:0;dp++;bitlen=8;} bitlen--; return (bitbuf>>bitlen)&1; }
static int getbyte(void){ int v=0; for(int i=0;i<8;i++) v=(v<<1)|getbit(); return v; }
static void start(void){ int i,j; bitlen=0;
  for(i=0;i<NCH;i++){freq[i]=1;son[i]=i+T;prnt[i+T]=i;}
  i=0;j=NCH; while(j<=R){freq[j]=freq[i]+freq[i+1];son[j]=i;prnt[i]=prnt[i+1]=j;i+=2;j++;}
  freq[T]=0xffff; prnt[R]=0; }
static void reconst(void){ int i,j,k; unsigned f,l;
  for(i=j=0;i<T;i++) if(son[i]>=T){freq[j]=(freq[i]+1)/2;son[j]=son[i];j++;}
  for(i=0,j=NCH;j<T;i+=2,j++){ k=i+1; f=freq[j]=freq[i]+freq[k];
    for(k=j-1;f<freq[k];k--); k++; l=(j-k);
    memmove(&freq[k+1],&freq[k],l*sizeof(freq[0])); freq[k]=f;
    memmove(&son[k+1],&son[k],l*sizeof(son[0])); son[k]=i; }
  for(i=0;i<T;i++){ if((k=son[i])>=T) prnt[k]=i; else prnt[k]=prnt[k+1]=i; } }
static void update(int c){ int i,j,l; unsigned k;
  if(freq[R]==MAXF) reconst();
  c=prnt[c+T];
  do{ k=++freq[c]; if(k>freq[l=c+1]){ while(k>freq[++l]); l--;
      freq[c]=freq[l]; freq[l]=k; i=son[c]; prnt[i]=l; if(i<T) prnt[i+1]=l;
      j=son[l]; son[l]=i; prnt[j]=c; if(j<T) prnt[j+1]=c; son[c]=j; c=l; }
  } while((c=prnt[c])!=0); }
static int dchar(void){ int c=son[R]; while(c<T){c+=getbit();c=son[c];} c-=T; update(c); return c; }
static int dpos(void){ int i=getbyte(),c=dcode[i]<<6,j=dlen[i]-2; while(j-->0) i=(i<<1)+getbit(); return c|(i&0x3f); }
int main(int argc,char**argv){
  if(argc<4){fprintf(stderr,"usage: unkart KART.DAT K.EXE outdir\n");return 1;}
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,2); datlen=ftell(f); fseek(f,0,0);
  uint8_t*d=malloc(datlen); if(fread(d,1,datlen,f)!=datlen){fprintf(stderr,"can't read %s\n",argv[1]);return 1;} fclose(f); dat=d;
  /* d_code/d_len tables live in K.EXE; find them by their known start pattern */
  f=fopen(argv[2],"rb"); fseek(f,0,2); long el=ftell(f); fseek(f,0,0); uint8_t*e=malloc(el); if(fread(e,1,el,f)!=(size_t)el){fprintf(stderr,"can't read %s\n",argv[2]);return 1;} fclose(f);
  int found=0; for(long i=0;i+512<el;i++){ if(e[i]==0&&e[i+31]==0&&e[i+32]==1&&e[i+255]==63&&e[i+0x128]==3&&e[i+0x127]==63){ memcpy(dcode,e+i,256); memcpy(dlen,e+i+0x128,256); found=1; break; } }
  if(!found){fprintf(stderr,"tables not found\n");return 1;}
  int flag=d[0]; int n=(d[1]|d[2]<<8)^0x4321; printf("flag %d, %d files\n",flag,n);
  mkdir(argv[3],0755);
  dp=3+18*n; uint8_t ring[N]; uint8_t*out=malloc(4<<20);
  for(int idx=0;idx<n;idx++){
    const uint8_t*b=d+3+18*idx; char name[15]; for(int k=0;k<14;k++) name[k]=b[k]^((idx+1)&255); name[14]=0;
    uint32_t sz=(b[14]|b[15]<<8|b[16]<<16|(uint32_t)b[17]<<24)^(0xccccccccu-(idx+1));
    size_t st=dp; start(); memset(ring,' ',N); int r=N-F; uint32_t o=0;
    while(o<sz){ int c=dchar(); if(c<256){out[o++]=c;ring[r]=c;r=(r+1)&(N-1);}
      else { int i=(r-dpos()-1)&(N-1), m=c-255+TH; for(int k=0;k<m;k++){ int c2=ring[(i+k)&(N-1)]; if(o<sz) out[o++]=c2; ring[r]=c2; r=(r+1)&(N-1);} } }
    char path[512]; snprintf(path,sizeof path,"%s/%s",argv[3],name);
    FILE*w=fopen(path,"wb"); fwrite(out,1,sz,w); fclose(w);
    if(idx<3||idx%100==0) printf("%4d %-13s %8u bytes (packed %zu)\n",idx,name,sz,dp-st);
  }
  printf("finished at byte %zu of %zu\n",dp,datlen);
  return 0; }
