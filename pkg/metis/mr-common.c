/*
 *	some Linux-specic functions are ported to windows here
 */
#include "mr-common.h"
#include <string.h>
#include <stdio.h>






getopt(int argc, char *const argv[], const char *optstring)
{
    
    
    
        
            
            

//                      return -2;
        }
        
              || argv[argIndex][2] != 0) {
            
            
            

//                      return '?';
        }
        
        
        
            
            
                 (((c > 'a' - 1) && (c < 'z' + 1))
                  || ((c > 'A' - 1) && (c < 'Z' + 1))))
            
            
                
                    
                        
                        
                        
                    }

                    else
                        
                    
                    
                    
                }
                
                /*
                 * No additional parameter
                 */
                
                
                
            }
            
        }
        
        
        

//              return '?';
    }
    
    
}

getFileMap(TCHAR * filename, char **data)
{
    
    
    
        CreateFile(filename, 
                   
    
        
        
        
    }
    
    
    
        CreateFileMapping(hFile, 
    
        
        
        
        
    }
    
    
    
}

closeFileMap()
{
    
    
    
    
    
}

