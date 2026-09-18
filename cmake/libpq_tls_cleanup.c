
/* Source2Root addition, Apache-2.0: libpq normally keeps this BIO_METHOD for
 * process lifetime. A statically linked, unloadable provider must release it
 * after all its connections and worker threads have been destroyed. This is
 * private to our libpq copy; never call OPENSSL_cleanup on a shared TLS runtime.
 */
void Source2RootPQReleaseTLSMethod(void);
void
Source2RootPQReleaseTLSMethod(void)
{
    BIO_meth_free(pgconn_bio_method_ptr);
    pgconn_bio_method_ptr = NULL;
}
