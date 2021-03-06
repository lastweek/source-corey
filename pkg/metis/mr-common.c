/*
 *	some Linux-specic functions are ported to windows here
 */
#include "mr-common.h"
#include <string.h>
#include <stdio.h>
char *optarg;
char *UNKNOWN_OPTION = "?";
static int argIndex = 0;
static HANDLE hFile, hFileMap;
DWORD gFileSize;
int
getopt(int argc, char *const argv[], const char *optstring)
{
    size_t optIndex;
    size_t optLen;
    while (argIndex < argc) {
        if (!argv[argIndex]) {
            argIndex++;
            continue;

//                      return -2;
        }
        if (argv[argIndex][0] != '-' || argv[argIndex][1] == 0
              || argv[argIndex][2] != 0) {
            optarg = UNKNOWN_OPTION;
            argIndex++;
            continue;

//                      return '?';
        }
        optIndex = 0;
        optLen = strlen(optstring);
        for (optIndex = 0; optIndex < optLen; optIndex++) {
            char c = optstring[optIndex];
            if (!
                 (((c > 'a' - 1) && (c < 'z' + 1))
                  || ((c > 'A' - 1) && (c < 'Z' + 1))))
            continue;
            if (argv[argIndex][1] == c) {
                if (optIndex + 1 < optLen && optstring[optIndex + 1] == ':') {
                    if (argIndex + 1 < argc) {
                        argIndex++;
                        optarg = argv[argIndex];
                        
                    }

                    else
                        optarg = UNKNOWN_OPTION;
                    argIndex++;
                    return c;
                    
                }
                
                /*
                 * No additional parameter
                 */
                argIndex++;
                return c;
                
            }
            
        }
        optarg = UNKNOWN_OPTION;
        argIndex++;
        continue;

//              return '?';
    }
    return -1;
    
}
int
getFileMap(TCHAR * filename, char **data)
{
    BY_HANDLE_FILE_INFORMATION FileInfo;
    closeFileMap();
    hFile =
        CreateFile(filename, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        printf("Could not open file (error %d)\n", GetLastError());
        return 0;
        
    }
    GetFileInformationByHandle(hFile, &FileInfo);
    gFileSize = FileInfo.nFileSizeLow;
    hFileMap =
        CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, 0, NULL);
    if (!hFileMap) {
        printf("File map error (error %d)\n", GetLastError());
        CloseHandle(hFile);
        return 0;
        
    }
    *data = (char *) MapViewOfFile(hFileMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    return 1;
    
}
void
closeFileMap()
{
    if (hFile != (HANDLE) ERROR_INVALID_HANDLE)
    CloseHandle(hFile);
    if (hFileMap != (HANDLE) ERROR_INVALID_HANDLE)
    CloseHandle(hFileMap);
    
}


