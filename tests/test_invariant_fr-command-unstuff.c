#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>

/* Forward declaration of the vulnerable function from fr-command-unstuff.c */
extern char* build_extraction_path(const char *root, const char *filename);

START_TEST(test_path_traversal_prevention)
{
    /* Invariant: Extracted file paths must never escape the root directory */
    const char *root = "/tmp/extract_root";
    const char *payloads[] = {
        "../../../etc/passwd",           /* Classic directory traversal */
        "....//....//etc/shadow",        /* Obfuscated traversal */
        "subdir/../../etc/hosts",        /* Relative escape attempt */
        "valid_file.txt"                 /* Valid input (baseline) */
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        char *resolved = build_extraction_path(root, payloads[i]);
        ck_assert_ptr_nonnull(resolved);
        
        /* Resolve both paths to canonical form for comparison */
        char root_real[PATH_MAX];
        char resolved_real[PATH_MAX];
        
        realpath(root, root_real);
        realpath(resolved, resolved_real);
        
        /* Assert: resolved path must start with root directory */
        ck_assert_int_eq(strncmp(resolved_real, root_real, strlen(root_real)), 0);
        
        /* Assert: no ".." sequences in final path */
        ck_assert_ptr_null(strstr(resolved_real, ".."));
        
        free(resolved);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("PathTraversal");

    tcase_add_test(tc_core, test_path_traversal_prevention);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}