#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>
#include "storage_manager.h"

static void clean_spiffs_dir(void)
{
    DIR *dir = opendir("./spiffs");
    if (!dir)
        return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        char path[280];
        snprintf(path, sizeof(path), "./spiffs/%s", name);
        remove(path);
    }
    closedir(dir);
}

void test_storage_manager_lifecycle(void)
{
    printf("Running test_storage_manager_lifecycle...\n");

    // Pre-clean folder to ensure fresh test state
    clean_spiffs_dir();

    // 1. Initialize storage manager
    bool init_ok = storage_manager_init();
    assert(init_ok);

    // Verify directory exists by calling get_info (init creates it if missing)
    storage_info_t info;
    storage_manager_get_info(&info);
    assert(info.file_count == 0);
    assert(info.txt_count == 0);
    assert(info.photo_count == 0);
    assert(info.total_bytes == 8 * 1024 * 1024);
    assert(info.used_bytes == 0);
    assert(info.free_bytes == info.total_bytes);

    // 2. Write text and binary files
    bool w1 = storage_manager_write_txt_file("file_a.txt", "Content of file A");
    assert(w1);
    bool w2 = storage_manager_write_txt_file("file_b.txt", "Content of file B, slightly longer.");
    assert(w2);
    bool w3 = storage_manager_write_txt_file("photo.bin", "1234567890"); // binary image mock
    assert(w3);
    bool w4 = storage_manager_write_txt_file("other.dat", "Some other data");
    assert(w4);

    // 3. Query storage info and verify classification
    storage_manager_get_info(&info);
    assert(info.file_count == 4);
    assert(info.txt_count == 2);
    assert(info.photo_count == 1); // photo.bin
    assert(info.used_bytes == strlen("Content of file A") + strlen("Content of file B, slightly longer.") +
                                  strlen("1234567890") + strlen("Some other data"));

    // Check individual files in info list
    bool found_a = false, found_b = false, found_photo = false, found_other = false;
    for (int i = 0; i < info.file_count; i++) {
        if (strcmp(info.files[i].name, "file_a.txt") == 0) {
            found_a = true;
            assert(info.files[i].size == strlen("Content of file A"));
            assert(strcmp(info.files[i].type, "document") == 0);
        } else if (strcmp(info.files[i].name, "file_b.txt") == 0) {
            found_b = true;
            assert(info.files[i].size == strlen("Content of file B, slightly longer."));
            assert(strcmp(info.files[i].type, "document") == 0);
        } else if (strcmp(info.files[i].name, "photo.bin") == 0) {
            found_photo = true;
            assert(info.files[i].size == strlen("1234567890"));
            assert(strcmp(info.files[i].type, "image") == 0);
        } else if (strcmp(info.files[i].name, "other.dat") == 0) {
            found_other = true;
            assert(info.files[i].size == strlen("Some other data"));
            assert(strcmp(info.files[i].type, "other") == 0);
        }
    }
    assert(found_a);
    assert(found_b);
    assert(found_photo);
    assert(found_other);

    // 4. List text files (should be sorted alphabetically)
    char txt_files[5][STORAGE_FILENAME_MAX_LEN];
    int  txt_cnt = storage_manager_list_txt_files(txt_files, 5);
    assert(txt_cnt == 2);
    assert(strcmp(txt_files[0], "file_a.txt") == 0);
    assert(strcmp(txt_files[1], "file_b.txt") == 0);

    // 5. Read file content
    char buf[128];
    int  read_bytes = storage_manager_read_txt_file("file_a.txt", buf, sizeof(buf));
    assert(read_bytes == (int)strlen("Content of file A"));
    assert(strcmp(buf, "Content of file A") == 0);

    // Try reading with smaller buffer to test truncation limit
    char small_buf[10];
    read_bytes = storage_manager_read_txt_file("file_b.txt", small_buf, sizeof(small_buf));
    assert(read_bytes == 9);
    assert(strcmp(small_buf, "Content o") == 0);

    // 6. Delete file
    bool del_ok = storage_manager_delete_file("file_a.txt");
    assert(del_ok);

    storage_manager_get_info(&info);
    assert(info.file_count == 3);
    assert(info.txt_count == 1);

    txt_cnt = storage_manager_list_txt_files(txt_files, 5);
    assert(txt_cnt == 1);
    assert(strcmp(txt_files[0], "file_b.txt") == 0);

    // 7. Deinitialize
    storage_manager_deinit();

    // Clean up files created during test
    clean_spiffs_dir();

    printf("test_storage_manager_lifecycle passed!\n");
}

void test_storage_manager_format(void)
{
    printf("Running test_storage_manager_format...\n");
    char out_buf[32];

    storage_manager_format_bytes(500, out_buf, sizeof(out_buf));
    assert(strcmp(out_buf, "500B") == 0);

    storage_manager_format_bytes(1024, out_buf, sizeof(out_buf));
    assert(strcmp(out_buf, "1.0KB") == 0);

    storage_manager_format_bytes(2048, out_buf, sizeof(out_buf));
    assert(strcmp(out_buf, "2.0KB") == 0);

    storage_manager_format_bytes(1024 * 1024, out_buf, sizeof(out_buf));
    assert(strcmp(out_buf, "1.0MB") == 0);

    storage_manager_format_bytes(1536 * 1024, out_buf, sizeof(out_buf));
    assert(strcmp(out_buf, "1.5MB") == 0);

    printf("test_storage_manager_format passed!\n");
}

int main(void)
{
    printf("Starting StorageManager tests...\n");
    test_storage_manager_lifecycle();
    test_storage_manager_format();
    printf("All StorageManager tests passed successfully!\n");
    return 0;
}
