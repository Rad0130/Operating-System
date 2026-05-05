#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define FS_MAGIC 0x56534653U
#define JOURNAL_MAGIC 0x4A524E4C // "JRNL"

#define BLOCK_SIZE        4096U
#define JOURNAL_BLOCKS      16U
#define REC_DATA            1
#define REC_COMMIT          2

// Structure Definitions
struct journal_header {
    uint32_t magic;
    uint32_t nbytes_used;
};

struct rec_header {
    uint16_t type;
    uint16_t size;
};

struct data_record {
    struct rec_header hdr;
    uint32_t block_no;
    uint8_t data[BLOCK_SIZE];
};

struct commit_record {
    struct rec_header hdr;
};

// Existing VSFS structures (Same as mkfs.c)
struct superblock {
    uint32_t magic, block_size, total_blocks, inode_count;
    uint32_t journal_block, inode_bitmap, data_bitmap, inode_start, data_start;
    uint8_t _pad[128 - 9 * 4];
};

struct inode {
    uint16_t type, links;
    uint32_t size, direct[8], ctime, mtime;
    uint8_t _pad[128 - (2 + 2 + 4 + 8 * 4 + 4 + 4)];
};

struct dirent {
    uint32_t inode;
    char name[28];
};

// Utility functions
static void die(const char *msg) { perror(msg); exit(EXIT_FAILURE); }

static void read_blk(int fd, uint32_t bno, void *buf) {
    if (pread(fd, buf, BLOCK_SIZE, bno * BLOCK_SIZE) != BLOCK_SIZE) die("pread");
}

static void write_blk(int fd, uint32_t bno, void *buf) {
    if (pwrite(fd, buf, BLOCK_SIZE, bno * BLOCK_SIZE) != BLOCK_SIZE) die("pwrite");
}

static int bitmap_test(const uint8_t *bitmap, uint32_t index) {
    return (bitmap[index / 8] >> (index % 8)) & 0x1;
}

static void bitmap_set(uint8_t *bitmap, uint32_t index) {
    bitmap[index / 8] |= (1 << (index % 8));
}

// Transaction helper
void append_to_journal(int fd, uint32_t journal_start, struct journal_header *jh, void *rec, uint16_t size) {
    uint32_t offset = (journal_start * BLOCK_SIZE) + jh->nbytes_used;
    if (jh->nbytes_used + size > JOURNAL_BLOCKS * BLOCK_SIZE) {
        printf("Journal full. Please run 'install' first.\n");
        exit(EXIT_FAILURE);
    }
    if (pwrite(fd, rec, size, offset) != size) die("pwrite journal");
    jh->nbytes_used += size;
}

void do_create(int fd, const char *name) {
    struct superblock sb;
    read_blk(fd, 0, &sb);

    // 1. Read current metadata
    uint8_t ibmap[BLOCK_SIZE];
    read_blk(fd, sb.inode_bitmap, ibmap);

    // Read inode table blocks
    uint8_t itable_block1[BLOCK_SIZE];
    read_blk(fd, sb.inode_start, itable_block1);
    struct inode *itable1 = (struct inode *)itable_block1;
    
    // Get root inode (inode 0)
    struct inode root_inode;
    memcpy(&root_inode, &itable1[0], sizeof(struct inode));
    
    // Read root directory data
    uint8_t root_data[BLOCK_SIZE];
    read_blk(fd, root_inode.direct[0], root_data);
    struct dirent *entries = (struct dirent *)root_data;

    // 2. Find free inode (start from 1 since 0 is root)
    int inum = -1;
    for (int i = 1; i < (int)sb.inode_count; i++) {
        if (!bitmap_test(ibmap, i)) { 
            inum = i; 
            break; 
        }
    }
    
    // Find free directory slot (skip the first 2 entries: "." and "..")
    int slot = -1;
    int max_entries = BLOCK_SIZE / sizeof(struct dirent);
    for (int i = 2; i < max_entries; i++) {  // Start from 2 to preserve "." and ".."
        if (entries[i].inode == 0) { 
            slot = i; 
            break; 
        }
    }

    if (inum == -1) { 
        printf("No free inodes\n"); 
        return; 
    }
    if (slot == -1) { 
        printf("No space in root directory\n"); 
        return; 
    }

    printf("Creating file '%s' at inode %d, directory slot %d\n", name, inum, slot);

    // 3. Create updated metadata blocks in memory
    // Update inode bitmap
    uint8_t new_ibmap[BLOCK_SIZE];
    memcpy(new_ibmap, ibmap, BLOCK_SIZE);
    bitmap_set(new_ibmap, inum);
    
    // Update root directory data block
    uint8_t new_root_data[BLOCK_SIZE];
    memcpy(new_root_data, root_data, BLOCK_SIZE);
    struct dirent *new_entries = (struct dirent *)new_root_data;
    
    // Add new directory entry
    new_entries[slot].inode = inum;
    strncpy(new_entries[slot].name, name, 27);
    new_entries[slot].name[27] = '\0';
    
    // Update root inode (size increased)
    struct inode new_root_inode = root_inode;
    new_root_inode.size += sizeof(struct dirent);
    new_root_inode.mtime = time(NULL);
    
    // Create new inode for the file
    struct inode new_file_inode;
    memset(&new_file_inode, 0, sizeof(new_file_inode));
    new_file_inode.type = 1; // File
    new_file_inode.links = 1;
    new_file_inode.size = 0;
    new_file_inode.ctime = new_file_inode.mtime = time(NULL);
    
    // Update inode table block 1 (contains root inode at index 0)
    uint8_t new_itable_block1[BLOCK_SIZE];
    memcpy(new_itable_block1, itable_block1, BLOCK_SIZE);
    struct inode *new_itable1 = (struct inode *)new_itable_block1;
    
    // Update root inode in the table
    memcpy(&new_itable1[0], &new_root_inode, sizeof(struct inode));
    
    // If new inode is in block 1, add it there
    int inodes_per_block = BLOCK_SIZE / sizeof(struct inode);
    if (inum < inodes_per_block) {
        memcpy(&new_itable1[inum], &new_file_inode, sizeof(struct inode));
    } else {
        // For inodes in second block, we need to handle that too
        uint8_t itable_block2[BLOCK_SIZE];
        uint8_t new_itable_block2[BLOCK_SIZE];
        read_blk(fd, sb.inode_start + 1, itable_block2);
        memcpy(new_itable_block2, itable_block2, BLOCK_SIZE);
        struct inode *new_itable2 = (struct inode *)new_itable_block2;
        memcpy(&new_itable2[inum - inodes_per_block], &new_file_inode, sizeof(struct inode));
        
        // Log second inode block if needed
        struct data_record dr2;
        dr2.hdr.type = REC_DATA;
        dr2.hdr.size = sizeof(struct data_record);
        dr2.block_no = sb.inode_start + 1;
        memcpy(dr2.data, new_itable_block2, BLOCK_SIZE);
        
        // We'll handle this in the journaling section
    }

    // 4. Journaling
    struct journal_header jh;
    pread(fd, &jh, sizeof(jh), sb.journal_block * BLOCK_SIZE);
    if (jh.magic != JOURNAL_MAGIC) {
        jh.magic = JOURNAL_MAGIC;
        jh.nbytes_used = sizeof(jh);
        pwrite(fd, &jh, sizeof(jh), sb.journal_block * BLOCK_SIZE);
    }

    struct data_record dr;
    dr.hdr.type = REC_DATA;
    dr.hdr.size = sizeof(struct data_record);

    // Log Inode Bitmap
    dr.block_no = sb.inode_bitmap;
    memcpy(dr.data, new_ibmap, BLOCK_SIZE);
    append_to_journal(fd, sb.journal_block, &jh, &dr, dr.hdr.size);

    // Log Inode Table Block 1 (always needed since root inode is updated)
    dr.block_no = sb.inode_start;
    memcpy(dr.data, new_itable_block1, BLOCK_SIZE);
    append_to_journal(fd, sb.journal_block, &jh, &dr, dr.hdr.size);

    // Log Root Directory Data Block
    dr.block_no = root_inode.direct[0];
    memcpy(dr.data, new_root_data, BLOCK_SIZE);
    append_to_journal(fd, sb.journal_block, &jh, &dr, dr.hdr.size);

    // If inode is in second block, log that too
    if (inum >= inodes_per_block) {
        uint8_t itable_block2[BLOCK_SIZE];
        uint8_t new_itable_block2[BLOCK_SIZE];
        read_blk(fd, sb.inode_start + 1, itable_block2);
        memcpy(new_itable_block2, itable_block2, BLOCK_SIZE);
        struct inode *new_itable2 = (struct inode *)new_itable_block2;
        memcpy(&new_itable2[inum - inodes_per_block], &new_file_inode, sizeof(struct inode));
        
        dr.block_no = sb.inode_start + 1;
        memcpy(dr.data, new_itable_block2, BLOCK_SIZE);
        append_to_journal(fd, sb.journal_block, &jh, &dr, dr.hdr.size);
    }

    // Commit
    struct commit_record cr;
    cr.hdr.type = REC_COMMIT;
    cr.hdr.size = sizeof(struct commit_record);
    append_to_journal(fd, sb.journal_block, &jh, &cr, cr.hdr.size);

    // Update Journal Header on disk
    pwrite(fd, &jh, sizeof(jh), sb.journal_block * BLOCK_SIZE);
    printf("Journaled creation of file '%s' at inode %d\n", name, inum);
}

void do_install(int fd) {
    struct superblock sb;
    read_blk(fd, 0, &sb);

    struct journal_header jh;
    pread(fd, &jh, sizeof(jh), sb.journal_block * BLOCK_SIZE);

    if (jh.magic != JOURNAL_MAGIC || jh.nbytes_used <= sizeof(jh)) {
        printf("Journal empty or invalid.\n"); 
        return;
    }

    uint32_t pos = sizeof(jh);
    uint32_t journal_base = sb.journal_block * BLOCK_SIZE;
    
    // Process all records in journal
    while (pos < jh.nbytes_used) {
        struct rec_header rh;
        pread(fd, &rh, sizeof(rh), journal_base + pos);
        
        if (rh.type == REC_DATA) {
            struct data_record dr;
            pread(fd, &dr, rh.size, journal_base + pos);
            // Write the block to its home location
            write_blk(fd, dr.block_no, dr.data);
            printf("Applied data record for block %u\n", dr.block_no);
        } 
        // Note: COMMIT records don't need any action during replay
        // They just mark the end of a transaction
        
        pos += rh.size;
    }

    // Checkpoint (Clear) Journal
    jh.nbytes_used = sizeof(jh);
    pwrite(fd, &jh, sizeof(jh), sb.journal_block * BLOCK_SIZE);
    printf("Journal installed and cleared.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { 
        printf("Usage: %s <create <name> | install>\n", argv[0]); 
        return 1; 
    }
    
    int fd = open("vsfs.img", O_RDWR);
    if (fd < 0) die("open vsfs.img");

    if (strcmp(argv[1], "create") == 0 && argc == 3) {
        do_create(fd, argv[2]);
    } else if (strcmp(argv[1], "install") == 0) {
        do_install(fd);
    } else {
        printf("Usage: %s <create <name> | install>\n", argv[0]);
    }
    
    close(fd);
    return 0;
}
