#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int is_prime(int n){ if(n < 2) return 0; for(int i = 2; i * i <= n; i++) if(n % i == 0) return 0; return 1; }

int main(void){
  char name[32]; int n;
  printf("Hello from my own C program!\n");
  printf("Your name? "); scanf("%s", name);
  for(char *p = name; *p; p++) *p = toupper(*p);
  printf("Up to what number? "); scanf("%d", &n);
  int *buf = malloc(n * sizeof(int)), cnt = 0;
  for(int i = 2; i <= n; i++) if(is_prime(i)) buf[cnt++] = i;
  printf("%s, there are %d primes <= %d:", name, cnt, n);
  for(int i = 0; i < cnt; i++) printf(" %d", buf[i]);
  printf("\n");
  free(buf);
  return cnt;
}
