#include "clar_libgit2.h"
#include "git2/refdb.h"
#include "refdb.h"

static git_repository *g_repo;
static int g_expected = 0;

void test_threads_refdb__initialize(void)
{
	g_repo = NULL;
}

void test_threads_refdb__cleanup(void)
{
	cl_git_sandbox_cleanup();
	g_repo = NULL;
}

#define REPEAT 20
#define THREADS 20

struct th_data {
	int id;
	const char *path;
};

static void *iterate_refs(void *arg)
{
	struct th_data *data = (struct th_data *) arg;
	git_reference_iterator *i;
	git_reference *ref;
	int count = 0;
	git_repository *repo;

	cl_git_pass(git_repository_open(&repo, data->path));
	cl_git_pass(git_reference_iterator_new(&i, repo));

	for (count = 0; !git_reference_next(&ref, i); ++count) {
		cl_assert(ref != NULL);
		git_reference_free(ref);
	}

	if (g_expected > 0)
		cl_assert_equal_i(g_expected, count);

	git_reference_iterator_free(i);

	git_repository_free(repo);
	giterr_clear();
	return arg;
}

static void *create_refs(void *arg)
{
	int i, error;
	struct th_data *data = (struct th_data *) arg;
	git_oid head;
	char name[128];
	git_reference *ref[10];
	git_repository *repo;

	cl_git_pass(git_repository_open(&repo, data->path));

	cl_git_pass(git_reference_name_to_id(&head, repo, "HEAD"));

	for (i = 0; i < 10; ++i) {
		p_snprintf(name, sizeof(name), "refs/heads/thread-%03d-%02d", data->id, i);
		cl_git_pass(git_reference_create(&ref[i], repo, name, &head, 0, NULL));

		if (i == 5) {
			git_refdb *refdb;
			cl_git_pass(git_repository_refdb(&refdb, repo));
			do {
				error = git_refdb_compress(refdb);
			} while (error == GIT_ELOCKED);
			git_refdb_free(refdb);
		}
	}

	for (i = 0; i < 10; ++i)
		git_reference_free(ref[i]);

	git_repository_free(repo);

	giterr_clear();
	return arg;
}

static void *delete_refs(void *arg)
{
	int i, error;
	struct th_data *data = (struct th_data *) arg;
	git_reference *ref;
	char name[128];
	git_repository *repo;

	cl_git_pass(git_repository_open(&repo, data->path));

	for (i = 0; i < 10; ++i) {
		p_snprintf(
			name, sizeof(name), "refs/heads/thread-%03d-%02d", (data->id) & ~0x3, i);

		if (!git_reference_lookup(&ref, repo, name)) {
			do {
				error = git_reference_delete(ref);
			} while (error == GIT_ELOCKED);
			cl_git_pass(error);
			git_reference_free(ref);
		}

		if (i == 5) {
			git_refdb *refdb;
			cl_git_pass(git_repository_refdb(&refdb, repo));
			do {
				error = git_refdb_compress(refdb);
			} while (error == GIT_ELOCKED);
			cl_git_pass(error);
			git_refdb_free(refdb);
		}
	}

	git_repository_free(repo);
	giterr_clear();
	return arg;
}

void test_threads_refdb__edit_while_iterate(void)
{
	int r, t;
	struct th_data th_data[THREADS];
	git_oid head;
	git_reference *ref;
	char name[128];
	git_refdb *refdb;

#ifdef GIT_THREADS
	git_thread th[THREADS];
#endif

	g_repo = cl_git_sandbox_init("testrepo2");

	cl_git_pass(git_reference_name_to_id(&head, g_repo, "HEAD"));

	/* make a bunch of references */

	for (r = 0; r < 50; ++r) {
		p_snprintf(name, sizeof(name), "refs/heads/starter-%03d", r);
		cl_git_pass(git_reference_create(&ref, g_repo, name, &head, 0, NULL));
		git_reference_free(ref);
	}

	cl_git_pass(git_repository_refdb(&refdb, g_repo));
	cl_git_pass(git_refdb_compress(refdb));
	git_refdb_free(refdb);

	g_expected = -1;

	g_repo = cl_git_sandbox_reopen(); /* reopen to flush caches */

	for (t = 0; t < THREADS; ++t) {
		void *(*fn)(void *arg);

		switch (t & 0x3) {
		case 0:  fn = create_refs;  break;
		case 1:  fn = delete_refs;  break;
		default: fn = iterate_refs; break;
		}

		th_data[t].id = t;
		th_data[t].path = git_repository_path(g_repo);

#ifdef GIT_THREADS
		cl_git_pass(git_thread_create(&th[t], NULL, fn, &th_data[t]));
#else
		fn(&th_data[t]);
#endif
	}

#ifdef GIT_THREADS
	for (t = 0; t < THREADS; ++t) {
		cl_git_pass(git_thread_join(&th[t], NULL));
	}

	memset(th, 0, sizeof(th));

	for (t = 0; t < THREADS; ++t) {
		th_data[t].id = t;
		cl_git_pass(git_thread_create(&th[t], NULL, iterate_refs, &th_data[t]));
	}

	for (t = 0; t < THREADS; ++t) {
		cl_git_pass(git_thread_join(&th[t], NULL));
	}
#endif
}
