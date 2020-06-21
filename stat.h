#define T_DIR  1   // Directory
#define T_FILE 2   // File
#define T_DEV  3   // Device

struct stat {
  short type;  // Type of file
  int dev;     // File system's disk device
  uint ino;    // Inode number
#ifdef CS333_P5
  ushort uid;
  ushort gid;
  union mode_t *stat_mode_t;
#endif // CS333_P5
  short nlink; // Number of links to file
  uint size;   // Size of file in bytes
};

