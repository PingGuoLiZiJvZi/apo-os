/*
 * mkfs.c — Host-side tool to create a disk image for apo-os MVP filesystem.
 *
 * Usage: mkfs <disk_image_path> <root_directory>
 *
 * Reads the host directory tree at <root_directory> and writes a formatted
 * disk image to <disk_image_path> conforming to the on-disk layout in fs.h.
 *
 * Compiled with HOST gcc, NOT the cross-compiler.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <assert.h>

/* ======== On-disk format constants (must match kernel/fs/fs.h) ======== */

#define MAGIC_NUM       0x1919810
#define FREE_INODE      0
#define DIR_INODE       1
#define FILE_INODE      2
#define BLOCK_SIZE      512
#define ADDRS_PER_INODE 12
#define DIRSIZ          28
#define NDIRECT         11
#define NINDIRECT       (BLOCK_SIZE / sizeof(uint32_t))  /* 128 */
#define MAXFILE         (NDIRECT + NINDIRECT)             /* 139 */

/* ======== Disk geometry ======== */

#define DISK_SIZE_MB    16
#define NBLOCKS         (DISK_SIZE_MB * 1024 * 1024 / BLOCK_SIZE) /* 32768 */

#define SB_BLOCK        1
#define INODE_START     2
/* 56 bytes per inode, 9 per block, 8 blocks → 72 inodes */
#define NINODES         72
#define IPB             (BLOCK_SIZE / sizeof(Inode))      /* 9 */
#define INODE_BLOCKS    ((NINODES + IPB - 1) / IPB)       /* 8 */
#define BMAP_START      (INODE_START + INODE_BLOCKS)       /* 10 */
/* Need ceil(NBLOCKS / BPB) bitmap blocks; BPB = BLOCK_SIZE*8 = 4096 */
#define BPB             (BLOCK_SIZE * 8)
#define BMAP_BLOCKS     ((NBLOCKS + BPB - 1) / BPB)       /* 8 */
#define DATA_START      (BMAP_START + BMAP_BLOCKS)         /* 18 */

/* ======== On-disk structures (packed to match kernel) ======== */

typedef struct {
    uint32_t magic;
    uint32_t ninodes;
    uint32_t nblocks;
    uint32_t inode_start;
    uint32_t bmap_start;
} SuperBlock;

typedef struct {
    uint16_t type;
    uint16_t nlink;
    uint32_t size;
    uint32_t addrs[ADDRS_PER_INODE];
} Inode;

typedef struct {
    uint32_t inum;
    char name[DIRSIZ];
} DirEntry;

/* ======== Global image buffer ======== */

static uint8_t disk[NBLOCKS * BLOCK_SIZE];  /* zeroed by BSS */

/* Next free inode (0 is unused, 1 = root) */
static uint32_t next_inum = 1;
/* Next free data block */
static uint32_t next_datablock = DATA_START;

/* ======== Helpers ======== */

static void write_block(uint32_t bno, const void *data) {
    memcpy(disk + bno * BLOCK_SIZE, data, BLOCK_SIZE);
}

static void read_block(uint32_t bno, void *data) {
    memcpy(data, disk + bno * BLOCK_SIZE, BLOCK_SIZE);
}

/* Mark block bno as used in the bitmap */
static void bmap_set(uint32_t bno) {
    uint32_t bmap_block = bno / BPB + BMAP_START;
    uint32_t bi = bno % BPB;
    uint8_t *bp = disk + bmap_block * BLOCK_SIZE;
    bp[bi / 8] |= (1 << (bi % 8));
}

/* Allocate a data block, returns block number */
static uint32_t balloc(void) {
    if (next_datablock >= NBLOCKS) {
        fprintf(stderr, "mkfs: out of data blocks\n");
        exit(1);
    }
    uint32_t b = next_datablock++;
    bmap_set(b);
    return b;
}

/* Allocate an inode, returns inum */
static uint32_t ialloc(uint16_t type) {
    uint32_t inum = next_inum++;
    if (inum >= NINODES) {
        fprintf(stderr, "mkfs: out of inodes\n");
        exit(1);
    }
    /* Write inode to image */
    Inode in;
    memset(&in, 0, sizeof(in));
    in.type = type;
    in.nlink = 1;
    in.size = 0;

    uint32_t block_no = INODE_START + inum / IPB;
    uint32_t offset   = (inum % IPB) * sizeof(Inode);
    memcpy(disk + block_no * BLOCK_SIZE + offset, &in, sizeof(Inode));
    return inum;
}

/* Read an inode from the image */
static void iget(uint32_t inum, Inode *ip) {
    uint32_t block_no = INODE_START + inum / IPB;
    uint32_t offset   = (inum % IPB) * sizeof(Inode);
    memcpy(ip, disk + block_no * BLOCK_SIZE + offset, sizeof(Inode));
}

/* Write an inode back to the image */
static void iput(uint32_t inum, const Inode *ip) {
    uint32_t block_no = INODE_START + inum / IPB;
    uint32_t offset   = (inum % IPB) * sizeof(Inode);
    memcpy(disk + block_no * BLOCK_SIZE + offset, ip, sizeof(Inode));
}

/*
 * Append raw bytes to an inode's data.
 * Handles direct + singly-indirect blocks.
 */
static void iappend(uint32_t inum, const void *data, uint32_t n) {
    Inode in;
    iget(inum, &in);

    const uint8_t *src = (const uint8_t *)data;
    uint32_t off = in.size;

    for (uint32_t tot = 0; tot < n; ) {
        uint32_t block_idx = off / BLOCK_SIZE;
        uint32_t disk_block;

        if (block_idx < NDIRECT) {
            if (in.addrs[block_idx] == 0)
                in.addrs[block_idx] = balloc();
            disk_block = in.addrs[block_idx];
        } else if (block_idx < MAXFILE) {
            /* Singly-indirect */
            if (in.addrs[NDIRECT] == 0)
                in.addrs[NDIRECT] = balloc(); /* allocate indirect table block */
            uint32_t indirect_buf[NINDIRECT];
            read_block(in.addrs[NDIRECT], indirect_buf);
            if (indirect_buf[block_idx - NDIRECT] == 0) {
                indirect_buf[block_idx - NDIRECT] = balloc();
                write_block(in.addrs[NDIRECT], indirect_buf);
            }
            disk_block = indirect_buf[block_idx - NDIRECT];
        } else {
            fprintf(stderr, "mkfs: file too large\n");
            exit(1);
        }

        uint32_t off_in_block = off % BLOCK_SIZE;
        uint32_t m = BLOCK_SIZE - off_in_block;
        if (n - tot < m)
            m = n - tot;

        memcpy(disk + disk_block * BLOCK_SIZE + off_in_block, src + tot, m);

        tot += m;
        off += m;
    }

    in.size = off;
    iput(inum, &in);
}

/*
 * Add a directory entry (name → child_inum) to directory dir_inum.
 */
static void dir_add(uint32_t dir_inum, const char *name, uint32_t child_inum) {
    DirEntry de;
    memset(&de, 0, sizeof(de));
    de.inum = child_inum;
    strncpy(de.name, name, DIRSIZ);
    iappend(dir_inum, &de, sizeof(DirEntry));
}

/*
 * Recursively populate the image from a host directory.
 * dir_inum = the already-allocated inode for this directory.
 * host_path = the host filesystem path to read from.
 */
static void populate_dir(uint32_t dir_inum, const char *host_path) {
    DIR *dp = opendir(host_path);
    if (!dp) {
        perror(host_path);
        exit(1);
    }

    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        /* Build full host path */
        char child_path[4096];
        snprintf(child_path, sizeof(child_path), "%s/%s", host_path, entry->d_name);

        struct stat st;
        if (stat(child_path, &st) < 0) {
            perror(child_path);
            exit(1);
        }

        if (S_ISDIR(st.st_mode)) {
            /* Allocate a directory inode */
            uint32_t child_inum = ialloc(DIR_INODE);
            dir_add(dir_inum, entry->d_name, child_inum);
            printf("  dir  inum=%2u  %s/\n", child_inum, child_path);
            /* Recurse */
            populate_dir(child_inum, child_path);
        } else if (S_ISREG(st.st_mode)) {
            /* Allocate a file inode */
            uint32_t child_inum = ialloc(FILE_INODE);
            dir_add(dir_inum, entry->d_name, child_inum);
            printf("  file inum=%2u  %s (%ld bytes)\n", child_inum, child_path, (long)st.st_size);

            /* Read file contents and append to inode */
            if (st.st_size > 0) {
                FILE *fp = fopen(child_path, "rb");
                if (!fp) {
                    perror(child_path);
                    exit(1);
                }
                uint8_t buf[4096];
                size_t nread;
                while ((nread = fread(buf, 1, sizeof(buf), fp)) > 0) {
                    iappend(child_inum, buf, nread);
                }
                fclose(fp);
            }
        }
        /* Skip other file types (symlinks, etc.) */
    }

    closedir(dp);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <disk.img> <root_dir>\n", argv[0]);
        return 1;
    }
    const char *img_path  = argv[1];
    const char *root_path = argv[2];

    printf("mkfs: creating %s from %s\n", img_path, root_path);
    printf("  BLOCK_SIZE=%d  NBLOCKS=%d  NINODES=%d\n", BLOCK_SIZE, NBLOCKS, NINODES);
    printf("  inode_start=%d  bmap_start=%d  data_start=%d\n", INODE_START, BMAP_START, DATA_START);

    /* Image is already zeroed (BSS) */

    /* ---- 1. Write SuperBlock at block 1 ---- */
    SuperBlock sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic       = MAGIC_NUM;
    sb.ninodes     = NINODES;
    sb.nblocks     = NBLOCKS;
    sb.inode_start = INODE_START;
    sb.bmap_start  = BMAP_START;
    memcpy(disk + SB_BLOCK * BLOCK_SIZE, &sb, sizeof(sb));

    /* ---- 2. Mark reserved blocks as used in bitmap ---- */
    /* Blocks 0 .. DATA_START-1 are reserved (boot, sb, inodes, bitmap) */
    for (uint32_t b = 0; b < DATA_START; b++) {
        bmap_set(b);
    }

    /* ---- 3. Allocate root inode (inum=1, type=DIR_INODE) ---- */
    uint32_t root_inum = ialloc(DIR_INODE);
    assert(root_inum == 1); /* ROOT_INODE must be 1 */
    printf("  root inum=%u\n", root_inum);

    /* ---- 4. Recursively populate from host directory ---- */
    populate_dir(root_inum, root_path);

    /* ---- 5. Write image to file ---- */
    printf("  data blocks used: %u (of %u available)\n",
           next_datablock - DATA_START, NBLOCKS - DATA_START);
    printf("  inodes used: %u (of %u)\n", next_inum - 1, NINODES);

    FILE *fp = fopen(img_path, "wb");
    if (!fp) {
        perror(img_path);
        return 1;
    }
    fwrite(disk, 1, sizeof(disk), fp);
    fclose(fp);

    printf("mkfs: wrote %s (%zu bytes)\n", img_path, sizeof(disk));
    return 0;
}
