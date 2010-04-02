/*
 * Ruby interface to remctl.
 *
 * Converts the libremctl C API into a Ruby interface.  Implements both the
 * simple and complex forms of the API.
 *
 * Original implementation by Anthony M. Martinez <twopir@nmt.edu>
 * Copyright 2010 Anthony M. Martinez <twopir@nmt.edu>
 * Copyright 2010 Board of Trustees, Leland Stanford Jr. University
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Anthony M. Martinez not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. Anthony M. Martinez makes no
 * representations about the suitability of this software for any purpose. It
 * is provided "as is" without express or implied warranty.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <config.h>
#include <portable/system.h>

#include <errno.h>
#include <ruby.h>
#include <sys/uio.h>

#include <client/remctl.h>

static VALUE cRemctl, cRemctlResult, eRemctlError, eRemctlNotOpen;

/* Since we can't have @ in our names here... */
static ID AAdefault_port, AAdefault_principal, Ahost, Aport, Aprincipal;

/* Map the remctl_output type constants to strings. */
const struct {
    enum remctl_output_type type;
    const char *name;
} OUTPUT_TYPE[] = {
    { REMCTL_OUT_OUTPUT, "output" },
    { REMCTL_OUT_STATUS, "status" },
    { REMCTL_OUT_ERROR,  "error"  },
    { REMCTL_OUT_DONE,   "done"   },
    { 0,                 NULL     }
};

/*
 * Used for the complex interface when a method that requires the remctl
 * connection be open is called.  Retrieves the underlying struct remctl
 * object and raises an exception if it is NULL.
 */
#define GET_REMCTL_OR_RAISE(obj, var)                                   \
    do {                                                                \
        Data_Get_Struct((obj), struct remctl, (var));                   \
        if ((var) == NULL)                                              \
            rb_raise(eRemctlNotOpen, "Connection is no longer open.");  \
    } while(0)


/*
 * Given a remctl_result pointer, return a Ruby Remctl::Result, unless the
 * remctl call had an error, in which case raise a Remctl::Error.  This is
 * used only internally by the extension and isn't exported as a method.
 */
static VALUE
rb_remctl_result_new(struct remctl_result *rr)
{
    VALUE result;

    if (rr->error)
        rb_raise(eRemctlError, "%s", rr->error);
    result = rb_class_new_instance(0, NULL, cRemctlResult);
    rb_iv_set(result, "@stderr", rb_str_new(rr->stderr_buf, rr->stderr_len));
    rb_iv_set(result, "@stdout", rb_str_new(rr->stdout_buf, rr->stdout_len));
    rb_iv_set(result, "@status", INT2FIX(rr->status));
    remctl_result_free(rr);
    return result;
}


/* call-seq:
 * Remctl::Result.new() -> #&lt;Remctl::Result&gt;
 *
 * Initialize a Remctl::Result object.
 */
static VALUE
rb_remctl_result_initialize(VALUE self)
{
    rb_define_attr(cRemctlResult, "stderr", 1, 0);
    rb_define_attr(cRemctlResult, "stdout", 1, 0);
    rb_define_attr(cRemctlResult, "status", 1, 0);
    return Qtrue;
}


/* call-seq:
 * Remctl.remctl(host, *args)   -> Remctl::Result
 *
 * Place a single Remctl call to the host, using the current default port and
 * principal.
 */
static VALUE
rb_remctl_remctl(int argc, VALUE argv[], VALUE self)
{
    VALUE vhost, vport, vprinc, vargs, tmp;
    unsigned int port;
    char *host, *princ;
    const char **args;
    struct remctl_result *result;
    int i, rc_argc;

    /*
     * Take the port and princ from the class instead of demanding that the
     * user specify "nil, nil" so often.
     */
    rb_scan_args(argc, argv, "1*", &vhost, &vargs);
    host   = StringValuePtr(vhost);
    vport  = rb_cvar_get(cRemctl, AAdefault_port);
    vprinc = rb_cvar_get(cRemctl, AAdefault_principal);
    port   = NIL_P(vport)  ? 0    : FIX2UINT(vport);
    princ  = NIL_P(vprinc) ? NULL : StringValuePtr(vprinc);

    /* Convert the remaining arguments to their underlying pointers. */
    rc_argc = RARRAY_LEN(vargs);
    args = ALLOC_N(const char *, rc_argc + 1);
    for (i = 0; i < rc_argc; i++) {
        tmp = rb_ary_entry(vargs, i);
        args[i] = StringValuePtr(tmp);
    }
    args[rc_argc] = NULL;

    /* Make the actual call. */
    result = remctl(host, port, princ, args);
    if (result == NULL)
        rb_raise(rb_eNoMemError, "remctl");
    return rb_remctl_result_new(result);
}


/* call-seq:
 * Remctl.default_port  -> 0
 *
 * Return the default port used for a Remctl simple connection or new instance
 * of a complex connection.  A value of 0 indicates the default port.
 */
static VALUE
rb_remctl_default_port_get(VALUE self)
{
    return rb_cvar_get(cRemctl, AAdefault_port);
}


/* call-seq:
 * Remctl.default_port = 0       -> 0
 * Remctl.default_port = 2617    -> 2617
 * Remctl.default_port = 65539   -> ArgError
 *
 * Change the default port used for a Remctl simple connection or new instance
 * of a complex connection.  A value of 0 indicates the default port.  Raises
 * +ArgError+ if the port number isn't sane.
 */
static VALUE
rb_remctl_default_port_set(VALUE self, VALUE new)
{
    unsigned int port;

    port = FIX2UINT(new);
    if (port > 65535)
        rb_raise(rb_eArgError, "Port number %u out of range", port);
    else
        rb_cvar_set(cRemctl, AAdefault_port, new, 0);
    return rb_cvar_get(cRemctl, AAdefault_port);
}


/* call-seq:
 * Remctl.default_principal  -> nil
 *
 * Return the default principal used for a Remctl simple connection or new
 * instance of a complex connection.
 */
static VALUE
rb_remctl_default_principal_get(VALUE self)
{
    return rb_cvar_get(cRemctl, AAdefault_principal);
}


/* call-seq:
 * Remctl.default_principal = "root@REALM"  -> "root@REALM"
 *
 * Set the principal used by default for a Remctl simple connection.  A value
 * of nil requests the library default.
 */
static VALUE
rb_remctl_default_principal_set(VALUE self, VALUE new)
{
    rb_cvar_set(cRemctl, AAdefault_principal, StringValue(new), 0);
    return rb_cvar_get(cRemctl, AAdefault_principal);
}


/*
 * Destructor for a Remctl object.  Closes the connection and frees the
 * underlying memory.
 */
static void
rb_remctl_destroy(void *r)
{
    if (r != NULL)
        remctl_close(r);
}


/*
 * Alloate a new object of the Remctl class and register it with the
 * destructor.
 */
static VALUE
rb_remctl_alloc(VALUE klass)
{
    struct remctl *r = NULL;

    return Data_Wrap_Struct(klass, NULL, rb_remctl_destroy, r);
}


/* call-seq:
 * r.close  -> nil
 *
 * Close a Remctl connection.  Any further operations (besides reopen) on the
 * object will raise Remctl::NotOpen.
 */
static VALUE
rb_remctl_close(VALUE self)
{
    struct remctl *r;

    GET_REMCTL_OR_RAISE(self, r);
    remctl_close(r);
    DATA_PTR(self) = NULL;
    return Qnil;
}


/* call-seq:
 * r.reopen  -> nil
 *
 * Reopen a Remctl connection to the stored host, port, and principal.  Raises
 * Remctl::Error if the connection fails.
 */
static VALUE
rb_remctl_reopen(VALUE self)
{
    struct remctl *r;
    VALUE vhost, vport, vprinc;
    char *host, *princ;
    unsigned int port;

    Data_Get_Struct(self, struct remctl, r);
    if (r != NULL)
        remctl_close(r);
    r = remctl_new();
    if (r == NULL)
        rb_raise(rb_eNoMemError, "remctl");

    /* Retrieve the stored host, port, and principal values. */
    vhost  = rb_ivar_get(self, Ahost);
    vport  = rb_ivar_get(self, Aport);
    vprinc = rb_ivar_get(self, Aprincipal);
    host   = StringValuePtr(vhost);
    port   = NIL_P(vport)  ? 0    : FIX2UINT(vport);
    princ  = NIL_P(vprinc) ? NULL : StringValuePtr(vprinc);

    /* Reopen the connection. */
    if (!remctl_open(r, host, port, princ))
        rb_raise(eRemctlError, "%s", rb_str_new2(remctl_error(r)));
    DATA_PTR(self) = r;
    return self;
}


/* call-seq:
 * r.command(*args)  -> nil
 *
 * Call a remote command.  Returns nil.
 *
 * Raises Remctl::Error in the event of failure, and Remctl::NotOpen if the
 * connection has been closed.
 */
static VALUE
rb_remctl_command(int argc, VALUE argv[], VALUE self)
{
    struct remctl *r;
    struct iovec *iov;
    int i;
    VALUE s;

    GET_REMCTL_OR_RAISE(self, rc);
    iov = ALLOC_N(struct iovec, argc);
    for (i = 0; i < argc; i++) {
        s = StringValue(argv[i]);
        iov[i].iov_base = RSTRING_PTR(s);
        iov[i].iov_len  = RSTRING_LEN(s);
    }
    if (!remctl_commandv(rc, iov, argc))
        rb_raise(eRemctlError, "%s", rb_str_new2(remctl_error(r)));
    return Qnil;
}


/*
 * Convert an enum remctl_output_type argument to a Ruby symbol.
 */
static VALUE
rb_remctl_type_intern(enum remctl_output_type type)
{
    int i;

    for (i = 0; OUTPUT_TYPE[i].name != NULL; i++)
        if (OUTPUT_TYPE[i].type == type)
            return ID2SYM(rb_intern(OUTPUT_TYPE[i].name));
    rb_bug("Fell off the end while looking up remctl output type %d!\n", type);
}


/* call-seq:
 * rc.command -> [type, output, stream, status, error]
 *
 * Retrieve the output tokens from the remote command.  Raises Remctl::Error
 * in the event of failure, and Remctl::NotOpen if the connection has been
 * closed.
 */
static VALUE
rb_remctl_output(VALUE self)
{
    struct remctl *r;
    struct remctl_output *output;

    GET_REMCTL_OR_RAISE(self, r);
    output = remctl_output(r);
    if (output == NULL)
        rb_raise(eRemctlError, "%s", rb_str_new2(remctl_error(r)));
    return rb_ary_new3(5, rb_remctl_type_intern(output->type),
                       rb_str_new(output->data, output->length),
                       INT2FIX(output->stream), INT2FIX(output->status),
                       INT2FIX(output->error));
}


/* call-seq:
 * Remctl.new(host, port=Remctl.default_port, princ=Remctl.default_principal) -> #&lt;Remctl&gt;
 * Remctl.new(host, port, princ) {|r| ...} -> nil
 *
 * Create and open a new Remctl complex instance to +host+.  Raises ArgError
 * if the port number is out of range, and Remctl::Error if the underlying
 * library raises one.  With a block, yield the instance, and ensure the
 * connection is closed at block exit.
 */
static VALUE
rb_remctl_initialize(int argc, VALUE argv[], VALUE self)
{
    VALUE vhost, vport, vprinc, vdefport, vdefprinc;
    unsigned int port;
    char *host, *princ;

    rb_define_attr(cRemctl, "host", 1, 0);
    rb_define_attr(cRemctl, "port", 1, 0);
    rb_define_attr(cRemctl, "principal", 1, 0);
    vdefport  = rb_cvar_get(cRemctl, AAdefault_port);
    vdefprinc = rb_cvar_get(cRemctl, AAdefault_principal);
    rb_scan_args(argc, argv, "12", &vhost, &vport, &vprinc);
    if (NIL_P(vport))
        vport  = vdefport;
    if (NIL_P(vprinc))
        vprinc = vdefprinc;
    host  = StringValuePtr(vhost);
    port  = NIL_P(vport)  ? 0    : FIX2UINT(vport);
    princ = NIL_P(vprinc) ? NULL : StringValuePtr(vprinc);
    if (port > 65535)
        rb_raise(rb_eArgError, "Port number %u out of range", port);

    /* Hold these in instance variables for reopen. */
    rb_ivar_set(self, Ahost, vhost);
    rb_ivar_set(self, Aport, vport);
    rb_ivar_set(self, Aprincipal, vprinc);
    rb_remctl_reopen(self);
    if (rb_block_given_p()) {
        rb_ensure(rb_yield, self, rb_remctl_close, self);
        return Qnil;
    } else {
        return self;
    }
}


/*
 * Ruby interface to the Remctl library for remote Kerberized command
 * execution.  This function does class setup and registers the methods and
 * variables.
 */
void
Init_Remctl(void)
{
    cRemctl = rb_define_class("Remctl", rb_cObject);
    rb_define_singleton_method(cRemctl, "remctl", rb_remctl_remctl, -1);

    /*
     * Allocate string constants used to refer to our class and instance
     * variables.
     */
    AAdefault_port      = rb_intern("@@default_port");
    AAdefault_principal = rb_intern("@@default_principal");
    Ahost               = rb_intern("@host");
    Aport               = rb_intern("@port");
    Aprincipal          = rb_intern("@principal");

    /* Default values for class variables. */
    rb_cvar_set(cRemctl, AAdefault_port, UINT2NUM(0), 0);
    rb_cvar_set(cRemctl, AAdefault_principal, Qnil, 0);

    /* Getter and setter methods for class variables. */
    rb_define_singleton_method(cRemctl, "default_port",
                               rb_remctl_default_port_get, 0);
    rb_define_singleton_method(cRemctl, "default_port=",
                               rb_remctl_default_port_set, 1);
    rb_define_singleton_method(cRemctl, "default_principal",
                               rb_remctl_default_principal_get, 0);
    rb_define_singleton_method(cRemctl, "default_principal=",
                               rb_remctl_default_principal_set, 1);

    /* Create the Remctl class. */
    rb_define_alloc_func(cRemctl, rb_remctl_alloc);
    rb_define_method(cRemctl, "initialize", rb_remctl_initialize, -1);
    rb_define_method(cRemctl, "close", rb_remctl_close, 0);
    rb_define_method(cRemctl, "reopen", rb_remctl_reopen, 0);
    rb_define_method(cRemctl, "command", rb_remctl_command, -1);
    rb_define_method(cRemctl, "output", rb_remctl_output, 0);

    /* Document-class: Remctl::Result
     *
     * Returned from a simple Remctl.remctl call.  Attributes:
     *
     * +stdout+:: String containing the returned standard output
     * +stderr+:: Same, but standard error.
     * +status+:: Fixnum of the command's exit status.
     */
    cRemctlResult = rb_define_class_under(cRemctl, "Result", rb_cObject);
    rb_define_method(cRemctlResult, "initialize",
                     rb_remctl_result_initialize, 0);

    /* Document-class: Remctl::Error
     *
     * Raised when Remctl has encountered either a protocol or network error.
     * The message comes from the library function +remctl_error+.
     */
    eRemctlError = rb_define_class_under(cRemctl, "Error", rb_eException);

    /* Document-class: Remctl::NotOpen
     *
     * Raised when using the complex interface and the connection has been
     * closed.
     */
    eRemctlNotOpen = rb_define_class_under(cRemctl, "NotOpen", rb_eException);
}
