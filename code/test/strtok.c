#include "string.h"
#include "stdlib.h"
#include "stdio.h"
 
int main()
{
  char str[]="/t0/aa/bb";
  char *delim = "/";
  char * pch;
  printf ("Splitting string \"%s\" into tokens:\n",str);
  pch = strtok(str,delim);
 while (pch != NULL)
  {
    printf ("%s\n",pch);
    pch = strtok (NULL, delim);
  }
printf("str = %s\n",str);
  pch = strtok(str,delim);
 while (pch != NULL)
  {
    printf ("%s\n",pch);
    pch = strtok (NULL, delim);
  }
      
//  system("pause");
  return 0;
}
