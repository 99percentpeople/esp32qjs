#include "bd/lfs_rambd.h"
#include "lfs.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define TEST_BLOCK_SIZE 512U
#define TEST_BLOCK_COUNT 128U
#define TEST_READ_SIZE 16U
#define TEST_PROG_SIZE 16U
#define TEST_CACHE_SIZE 64U
#define TEST_LOOKAHEAD_SIZE 16U

typedef struct {
    uint8_t storage[TEST_BLOCK_SIZE * TEST_BLOCK_COUNT];
    lfs_rambd_t block_device;
    struct lfs_rambd_config block_config;
    struct lfs_config config;
} littlefs_fixture_t;

static void fixture_init(littlefs_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->block_config = (struct lfs_rambd_config){
        .read_size = TEST_READ_SIZE,
        .prog_size = TEST_PROG_SIZE,
        .erase_size = TEST_BLOCK_SIZE,
        .erase_count = TEST_BLOCK_COUNT,
        .buffer = fixture->storage,
    };
    fixture->config = (struct lfs_config){
        .context = &fixture->block_device,
        .read = lfs_rambd_read,
        .prog = lfs_rambd_prog,
        .erase = lfs_rambd_erase,
        .sync = lfs_rambd_sync,
        .read_size = TEST_READ_SIZE,
        .prog_size = TEST_PROG_SIZE,
        .block_size = TEST_BLOCK_SIZE,
        .block_count = TEST_BLOCK_COUNT,
        .block_cycles = 100,
        .cache_size = TEST_CACHE_SIZE,
        .lookahead_size = TEST_LOOKAHEAD_SIZE,
    };
    assert(lfs_rambd_create(&fixture->config,
                            &fixture->block_config) == 0);
}

static void fixture_deinit(littlefs_fixture_t *fixture)
{
    assert(lfs_rambd_destroy(&fixture->config) == 0);
}

static void write_committed_records(lfs_t *lfs, const char *text, size_t count)
{
    lfs_file_t file;
    size_t length = strlen(text);
    size_t index;

    assert(lfs_file_open(lfs, &file, "workspace/index.js",
                         LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND) == 0);
    for (index = 0; index < count; ++index) {
        assert(lfs_file_write(lfs, &file, text, length) ==
               (lfs_ssize_t)length);
        assert(lfs_file_sync(lfs, &file) == 0);
    }
    assert(lfs_file_close(lfs, &file) == 0);
}

static void assert_records(lfs_t *lfs, const char *text, size_t count)
{
    lfs_file_t file;
    char buffer[32];
    size_t length = strlen(text);
    size_t index;

    assert(length <= sizeof(buffer));
    assert(lfs_file_open(lfs, &file, "workspace/index.js", LFS_O_RDONLY) == 0);
    for (index = 0; index < count; ++index) {
        memset(buffer, 0, sizeof(buffer));
        assert(lfs_file_read(lfs, &file, buffer, length) ==
               (lfs_ssize_t)length);
        assert(memcmp(buffer, text, length) == 0);
    }
    assert(lfs_file_close(lfs, &file) == 0);
}

static void test_partial_metadata_program_preserves_committed_workspace(void)
{
    littlefs_fixture_t fixture;
    lfs_t lfs;
    lfs_dir_t directory;
    lfs_block_t metadata_block;
    lfs_off_t metadata_offset;
    uint8_t block[TEST_BLOCK_SIZE];

    fixture_init(&fixture);
    assert(lfs_format(&lfs, &fixture.config) == 0);
    assert(lfs_mount(&lfs, &fixture.config) == 0);
    assert(lfs_mkdir(&lfs, "workspace") == 0);
    write_committed_records(&lfs, "hello", 5);
    assert_records(&lfs, "hello", 5);
    assert(lfs_unmount(&lfs) == 0);

    assert(lfs_mount(&lfs, &fixture.config) == 0);
    assert(lfs_dir_open(&lfs, &directory, "workspace") == 0);
    metadata_block = directory.m.pair[0];
    metadata_offset = directory.m.off;
    assert(lfs_dir_close(&lfs, &directory) == 0);
    assert(lfs_unmount(&lfs) == 0);

    assert(fixture.config.read(&fixture.config, metadata_block, 0,
                               block, sizeof(block)) == 0);
    assert(metadata_offset < sizeof(block));
    block[metadata_offset] ^= 0x5aU;
    assert(fixture.config.erase(&fixture.config, metadata_block) == 0);
    assert(fixture.config.prog(&fixture.config, metadata_block, 0,
                               block, sizeof(block)) == 0);

    assert(lfs_mount(&lfs, &fixture.config) == 0);
    assert_records(&lfs, "hello", 5);
    write_committed_records(&lfs, "goodbye", 5);
    assert(lfs_unmount(&lfs) == 0);

    assert(lfs_mount(&lfs, &fixture.config) == 0);
    assert_records(&lfs, "hello", 5);
    {
        lfs_file_t file;
        char buffer[8];
        size_t index;

        assert(lfs_file_open(&lfs, &file, "workspace/index.js",
                             LFS_O_RDONLY) == 0);
        assert(lfs_file_seek(&lfs, &file, 25, LFS_SEEK_SET) == 25);
        for (index = 0; index < 5; ++index) {
            assert(lfs_file_read(&lfs, &file, buffer, 7) == 7);
            assert(memcmp(buffer, "goodbye", 7) == 0);
        }
        assert(lfs_file_close(&lfs, &file) == 0);
    }
    assert(lfs_unmount(&lfs) == 0);
    fixture_deinit(&fixture);
}

static void test_corrupt_media_is_not_mounted_until_explicit_reformat(void)
{
    littlefs_fixture_t fixture;
    lfs_t lfs;

    fixture_init(&fixture);
    assert(lfs_format(&lfs, &fixture.config) == 0);
    assert(lfs_mount(&lfs, &fixture.config) == 0);
    assert(lfs_mkdir(&lfs, "workspace") == 0);
    write_committed_records(&lfs, "trusted", 1);
    assert(lfs_unmount(&lfs) == 0);

    memset(fixture.storage, 0xa5, sizeof(fixture.storage));
    assert(lfs_mount(&lfs, &fixture.config) < 0);

    assert(lfs_format(&lfs, &fixture.config) == 0);
    assert(lfs_mount(&lfs, &fixture.config) == 0);
    {
        lfs_file_t file;

        assert(lfs_file_open(&lfs, &file, "workspace/index.js",
                             LFS_O_RDONLY) == LFS_ERR_NOENT);
    }
    assert(lfs_unmount(&lfs) == 0);
    fixture_deinit(&fixture);
}

int main(void)
{
    test_partial_metadata_program_preserves_committed_workspace();
    test_corrupt_media_is_not_mounted_until_explicit_reformat();
    return 0;
}
