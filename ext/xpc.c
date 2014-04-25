#include "ruby.h"
#include "ruby/intern.h"

#import <CoreFoundation/CoreFoundation.h>
#import <xpc/xpc.h>

static VALUE xpc_alloc();
static VALUE xpc_init();
static VALUE xpc_emit();
static VALUE xpc_on();
static VALUE xpc_connect();
static VALUE xpc_disconnect();
static VALUE xpc_dictionary_to_hash();
static VALUE xpc_object_to_value();
static VALUE xpc_array_to_array();
static void xpc_handle_event();
static void xpc_dealloc();
static xpc_object_t rb_value_to_xpc_object();
static xpc_object_t rb_array_to_xpc_object();
static xpc_object_t rb_hash_to_xpc_object();
static int hash_iter_cb();

struct xpc_connection {
  xpc_connection_t connection;
};

void Init_xpc() {
  VALUE xpc = rb_define_class("XPC", rb_cObject);

  rb_define_alloc_func(xpc, xpc_alloc);

  rb_define_method(xpc, "initialize", xpc_init, 1);
  rb_define_method(xpc, "emit",       xpc_emit, 1);
  rb_define_method(xpc, "on",         xpc_on, 1);
  rb_define_method(xpc, "disconnect", xpc_disconnect, 0);
  rb_define_method(xpc, "connect",    xpc_connect, 0);

}

static VALUE xpc_alloc(VALUE self) {
  struct xpc_connection *connection = malloc(sizeof(struct xpc_connection));
  return Data_Wrap_Struct(self, NULL, xpc_dealloc, connection);
}

static VALUE xpc_init(VALUE self, VALUE service_name) {
  Check_Type(service_name, T_STRING);
  rb_iv_set(self, "service_name", service_name);
  rb_iv_set(self, "callbacks", rb_hash_new());

  return self;
}

static VALUE xpc_emit(VALUE self, VALUE payload) {
  xpc_connection_t *connection;
  Data_Get_Struct(self, xpc_connection_t, connection);

  xpc_object_t message = rb_value_to_xpc_object(payload);

  xpc_connection_send_message(*connection, message);

  return self;
}

static VALUE xpc_on(VALUE self, VALUE key) {
  Check_Type(key, T_SYMBOL);

  VALUE proc = rb_block_proc();
  VALUE callbacks = rb_iv_get(self, "callbacks");

  rb_hash_aset(callbacks, key, proc);

  return self;
}

static VALUE xpc_disconnect(VALUE self) {
  xpc_connection_t *connection;
  Data_Get_Struct(self, xpc_connection_t, connection);
  xpc_connection_suspend(*connection);

  return Qtrue;
}

static VALUE xpc_connect(VALUE self) {
  struct xpc_connection *connection;
  Data_Get_Struct(self, struct xpc_connection, connection);

  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    VALUE ivar     = rb_iv_get(self, "service_name");
    char * service = StringValueCStr(ivar);

    connection->connection = xpc_connection_create_mach_service(service, dispatch_get_main_queue(), XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);

    xpc_connection_set_event_handler(connection->connection, ^(xpc_object_t event) {
      xpc_retain(event);
      xpc_handle_event(self, event);
    });
  });

  xpc_connection_resume(connection->connection);

  for (;;) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 5, TRUE);
  }

  return Qtrue;
}

static void xpc_handle_event(VALUE self, xpc_object_t event) {
  VALUE callbacks = rb_iv_get(self, "callbacks");
  VALUE callback;
  VALUE payload;

  xpc_type_t type = xpc_get_type(event);
  if (type == XPC_TYPE_ERROR) {
    const char * message = "unknown";

    if (event == XPC_ERROR_CONNECTION_INTERRUPTED) {
      message = "connection interrupted";
    } else if (event == XPC_ERROR_CONNECTION_INVALID) {
      message = "connection invalid";
    }

    callback   = rb_hash_aref(callbacks, ID2SYM(rb_intern("error")));
    payload    = rb_str_new2(message);
    VALUE args = rb_ary_new();

    rb_ary_push(args, payload);

    rb_proc_call(callback, args);


  } else if (type == XPC_TYPE_DICTIONARY) {

    callback = rb_hash_aref(callbacks, ID2SYM(rb_intern("message")));
    payload  = xpc_dictionary_to_hash(event);

    rb_proc_call(callback, payload);
  }

  xpc_release(event);
}

static void xpc_dealloc(void * connection) {
  xpc_release((xpc_connection_t)connection);
}

static VALUE xpc_dictionary_to_hash(xpc_object_t dictionary) {
  VALUE hash = rb_hash_new();

  xpc_dictionary_apply(dictionary, ^bool(const char *key, xpc_object_t value) {
    rb_hash_aset(hash, rb_str_new2(key), xpc_object_to_value(value));

    return true;
  });

  return hash;
}

static VALUE xpc_object_to_value(xpc_object_t value) {
  VALUE ret = 0;

  xpc_type_t type = xpc_get_type(value);

  if (type == XPC_TYPE_INT64) {
    ret = NUM2INT(xpc_int64_get_value(value));
  } else if(type == XPC_TYPE_STRING) {
    ret = rb_str_new2(xpc_string_get_string_ptr(value));
  } else if(type == XPC_TYPE_DICTIONARY) {
    ret = xpc_dictionary_to_hash(value);
  } else if(type == XPC_TYPE_ARRAY) {
    ret = xpc_array_to_array(value);
  } else if(type == XPC_TYPE_DATA) {
    ret = rb_str_new((char *)xpc_data_get_bytes_ptr(value), xpc_data_get_length(value));
  } else if(type == XPC_TYPE_UUID) {
    ret = rb_str_new((char *)xpc_uuid_get_bytes(value), sizeof(uuid_t));
  } else {
    rb_warn("Could not convert xpc object to value!");
  }

  return ret;
}

static VALUE xpc_array_to_array(xpc_object_t object) {
  VALUE array = rb_ary_new();

  xpc_array_apply(object, ^bool(size_t index, xpc_object_t value) {
    rb_ary_push(array, xpc_object_to_value(value));
    return true;
  });

  return array;
}

static xpc_object_t rb_value_to_xpc_object(VALUE value) {
  VALUE type = CLASS_OF(value);
  VALUE ret  = 0;

  if(type == rb_cFixnum) {
    ret = xpc_int64_create(value);
  } else if(type == rb_cString) {
    VALUE pattern = "[A-F0-9]{8}-[A-F0-9]{4}-[A-F0-9]{4}-[A-F0-9]{4}-[A-F0-9]{12}";
    VALUE regex   = rb_reg_new_str(pattern, 0);
    if(rb_reg_match(value, regex)) {
      ret = xpc_uuid_create(*StringValueCStr(value));
    } else {
      ret = xpc_string_create(StringValueCStr(value));
    }
  } else if(type == rb_cArray) {
    ret = rb_array_to_xpc_object(value);
  } else if (type == rb_cHash) {
    ret = rb_hash_to_xpc_object(value);
  }

  return ret;
}

static xpc_object_t rb_array_to_xpc_object(VALUE array) {
  xpc_object_t xpc_array = xpc_array_create(NULL, 0);
  long length = RARRAY_LEN(array);

  for (long i = 0; i < length; i++) {
    xpc_object_t object = rb_value_to_xpc_object(rb_ary_entry(array, i));
    xpc_array_append_value(xpc_array, object);

    if(object) xpc_release(object);
  }

  return xpc_array;
}

static xpc_object_t rb_hash_to_xpc_object(VALUE hash) {
  xpc_object_t object = xpc_dictionary_create(NULL, NULL, 0);

  VALUE keys = rb_ary_new();

  rb_hash_foreach(hash, hash_iter_cb, keys);

  long length = RARRAY_LEN(keys);

  for (long i = 0; i< length; i++) {
    VALUE key   = rb_ary_entry(hash, i);
    VALUE value = rb_hash_aref(hash, key);

    VALUE string = rb_any_to_s(key);

    xpc_dictionary_set_value(object, StringValueCStr(string), rb_value_to_xpc_object(value));
  }

  return  object;
}

static int hash_iter_cb (VALUE key, VALUE value, VALUE array) {
  rb_ary_push(array, key);

  return ST_CONTINUE;
}
