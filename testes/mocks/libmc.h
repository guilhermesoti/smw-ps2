#ifndef TEST_LIBMC_H
#define TEST_LIBMC_H

#define MC_TYPE_NONE 0
#define MC_TYPE_PS2 2

int mcOpen(int port, int slot, const char *name, int flags);
int mcDelete(int port, int slot, const char *name);
int mcRead(int fd, void *buffer, int size);
int mcSeek(int fd, int offset, int origin);
int mcWrite(int fd, const void *buffer, int size);
int mcClose(int fd);
int mcSync(int mode, int *command, int *result);
int mcMkDir(int port, int slot, const char *name);
int mcGetInfo(int port, int slot, int *type, int *free_clusters, int *format);
typedef struct { unsigned char _Create[8], _Modify[8]; unsigned FileSizeByte;
                 unsigned short AttrFile, Reserve1; unsigned Reserve2, PdaAplNo;
                 unsigned char EntryName[32]; } sceMcTblGetDir;
int mcSetFileInfo(int port, int slot, const char *name, const sceMcTblGetDir *info, unsigned flags);

#endif
