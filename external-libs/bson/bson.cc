#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <v8.h>
#include <node.h>
#include <node_version.h>
#include <node_buffer.h>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "bson.h"
#include "long.h"
#include "timestamp.h"
#include "objectid.h"
#include "binary.h"
#include "code.h"
#include "dbref.h"
#include "symbol.h"
#include "minkey.h"
#include "maxkey.h"
#include "double.h"

using namespace v8;
using namespace node;

// BSON DATA TYPES
const uint32_t BSON_DATA_NUMBER = 1;
const uint32_t BSON_DATA_STRING = 2;
const uint32_t BSON_DATA_OBJECT = 3;
const uint32_t BSON_DATA_ARRAY = 4;
const uint32_t BSON_DATA_BINARY = 5;
const uint32_t BSON_DATA_OID = 7;
const uint32_t BSON_DATA_BOOLEAN = 8;
const uint32_t BSON_DATA_DATE = 9;
const uint32_t BSON_DATA_NULL = 10;
const uint32_t BSON_DATA_REGEXP = 11;
const uint32_t BSON_DATA_CODE = 13;
const uint32_t BSON_DATA_SYMBOL = 14;
const uint32_t BSON_DATA_CODE_W_SCOPE = 15;
const uint32_t BSON_DATA_INT = 16;
const uint32_t BSON_DATA_TIMESTAMP = 17;
const uint32_t BSON_DATA_LONG = 18;
const uint32_t BSON_DATA_MIN_KEY = 0xff;
const uint32_t BSON_DATA_MAX_KEY = 0x7f;

const int32_t BSON_INT32_MAX = (int32_t)2147483647L;
const int32_t BSON_INT32_MIN = (int32_t)(-1) * 2147483648L;

// BSON BINARY DATA SUBTYPES
const uint32_t BSON_BINARY_SUBTYPE_FUNCTION = 1;
const uint32_t BSON_BINARY_SUBTYPE_BYTE_ARRAY = 2;
const uint32_t BSON_BINARY_SUBTYPE_UUID = 3;
const uint32_t BSON_BINARY_SUBTYPE_MD5 = 4;
const uint32_t BSON_BINARY_SUBTYPE_USER_DEFINED = 128;

static Handle<Value> VException(const char *msg) {
    HandleScope scope;
    return ThrowException(Exception::Error(String::New(msg)));
  };

Persistent<FunctionTemplate> BSON::constructor_template;

void BSON::Initialize(v8::Handle<v8::Object> target) {
  // Grab the scope of the call from Node
  HandleScope scope;
  // Define a new function template
  Local<FunctionTemplate> t = FunctionTemplate::New(New);
  constructor_template = Persistent<FunctionTemplate>::New(t);
  constructor_template->InstanceTemplate()->SetInternalFieldCount(1);
  constructor_template->SetClassName(String::NewSymbol("BSON"));
  
  // Class methods
  NODE_SET_METHOD(constructor_template->GetFunction(), "serialize", BSONSerialize);  
  NODE_SET_METHOD(constructor_template->GetFunction(), "serializeWithBufferAndIndex", SerializeWithBufferAndIndex);
  NODE_SET_METHOD(constructor_template->GetFunction(), "deserialize", BSONDeserialize);  
  NODE_SET_METHOD(constructor_template->GetFunction(), "encodeLong", EncodeLong);  
  NODE_SET_METHOD(constructor_template->GetFunction(), "toLong", ToLong);
  NODE_SET_METHOD(constructor_template->GetFunction(), "toInt", ToInt);
  NODE_SET_METHOD(constructor_template->GetFunction(), "calculateObjectSize", CalculateObjectSize);

  target->Set(String::NewSymbol("BSON"), constructor_template->GetFunction());
}

// Create a new instance of BSON and assing it the existing context
Handle<Value> BSON::New(const Arguments &args) {
  HandleScope scope;
  
  BSON *bson = new BSON();
  bson->Wrap(args.This());
  return args.This();
}

Handle<Value> BSON::SerializeWithBufferAndIndex(const Arguments &args) {
  HandleScope scope;  

  //BSON.serializeWithBufferAndIndex = function serializeWithBufferAndIndex(object, checkKeys, buffer, index) {
  // Ensure we have the correct values
  if(args.Length() > 5) return VException("Four or five parameters required [object, boolean, Buffer, int] or [object, boolean, Buffer, int, boolean]");
  if(args.Length() == 4 && !args[0]->IsObject() && !args[1]->IsBoolean() && !Buffer::HasInstance(args[2]) && !args[3]->IsUint32()) return VException("Four parameters required [object, boolean, Buffer, int]");
  if(args.Length() == 5 && !args[0]->IsObject() && !args[1]->IsBoolean() && !Buffer::HasInstance(args[2]) && !args[3]->IsUint32() && !args[4]->IsBoolean()) return VException("Four parameters required [object, boolean, Buffer, int, boolean]");

  // Define pointer to data
  char *data;
  uint32_t length;      
  // Unpack the object
  Local<Object> obj = args[2]->ToObject();

  // Unpack the buffer object and get pointers to structures
  #if NODE_MAJOR_VERSION == 0 && NODE_MINOR_VERSION < 3
    Buffer *buffer = ObjectWrap::Unwrap<Buffer>(obj);
    data = buffer->data();
    length = buffer->length();
  #else
    data = Buffer::Data(obj);
    length = Buffer::Length(obj);
  #endif
  
  uint32_t object_size = 0;
  // Calculate the total size of the document in binary form to ensure we only allocate memory once
  if(args.Length() == 5) {
    object_size = BSON::calculate_object_size(args[0], args[4]->BooleanValue());    
  } else {
    object_size = BSON::calculate_object_size(args[0], false);    
  }
  
  // Unpack the index variable
  Local<Uint32> indexObject = args[3]->ToUint32();
  uint32_t index = indexObject->Value();

  // Allocate the memory needed for the serializtion
  char *serialized_object = (char *)malloc(object_size * sizeof(char));  

  // Catch any errors
  try {
    // Check if we have a boolean value
    bool check_key = false;
    if(args.Length() >= 4 && args[1]->IsBoolean()) {
      check_key = args[1]->BooleanValue();
    }
    
    bool serializeFunctions = false;
    if(args.Length() == 5) {
      serializeFunctions = args[4]->BooleanValue();
    }
    
    // Serialize the object
    BSON::serialize(serialized_object, 0, Null(), args[0], check_key, serializeFunctions);
  } catch(char *err_msg) {
    // Free up serialized object space
    free(serialized_object);
    V8::AdjustAmountOfExternalAllocatedMemory(-object_size);
    // Throw exception with the string
    Handle<Value> error = VException(err_msg);
    // free error message
    free(err_msg);
    // Return error
    return error;
  }

  for(int i = 0; i < object_size; i++) {
    *(data + index + i) = *(serialized_object + i);
  }
  
  return scope.Close(Uint32::New(index + object_size - 1));
}

Handle<Value> BSON::BSONSerialize(const Arguments &args) {
  HandleScope scope;

  if(args.Length() == 1 && !args[0]->IsObject()) return VException("One, two or tree arguments required - [object] or [object, boolean] or [object, boolean, boolean]");
  if(args.Length() == 2 && !args[0]->IsObject() && !args[1]->IsBoolean()) return VException("One, two or tree arguments required - [object] or [object, boolean] or [object, boolean, boolean]");
  if(args.Length() == 3 && !args[0]->IsObject() && !args[1]->IsBoolean() && !args[2]->IsBoolean()) return VException("One, two or tree arguments required - [object] or [object, boolean] or [object, boolean, boolean]");
  if(args.Length() == 4 && !args[0]->IsObject() && !args[1]->IsBoolean() && !args[2]->IsBoolean() && !args[3]->IsBoolean()) return VException("One, two or tree arguments required - [object] or [object, boolean] or [object, boolean, boolean] or [object, boolean, boolean, boolean]");
  if(args.Length() > 4) return VException("One, two, tree or four arguments required - [object] or [object, boolean] or [object, boolean, boolean] or [object, boolean, boolean, boolean]");

  uint32_t object_size = 0;
  // Calculate the total size of the document in binary form to ensure we only allocate memory once
  // With serialize function
  if(args.Length() == 4) {
    object_size = BSON::calculate_object_size(args[0], args[3]->BooleanValue());    
  } else {
    object_size = BSON::calculate_object_size(args[0], false);        
  }

  // Allocate the memory needed for the serializtion
  char *serialized_object = (char *)malloc(object_size * sizeof(char));  
  // Catch any errors
  try {
    // Check if we have a boolean value
    bool check_key = false;
    if(args.Length() >= 3 && args[1]->IsBoolean()) {
      check_key = args[1]->BooleanValue();
    }

    // Check if we have a boolean value
    bool serializeFunctions = false;
    if(args.Length() == 4 && args[1]->IsBoolean()) {
      serializeFunctions = args[3]->BooleanValue();
    }
    
    // Serialize the object
    BSON::serialize(serialized_object, 0, Null(), args[0], check_key, serializeFunctions);      
  } catch(char *err_msg) {
    // Free up serialized object space
    free(serialized_object);
    V8::AdjustAmountOfExternalAllocatedMemory(-object_size);
    // Throw exception with the string
    Handle<Value> error = VException(err_msg);
    // free error message
    free(err_msg);
    // Return error
    return error;
  }

  // Write the object size
  BSON::write_int32((serialized_object), object_size);  

  // If we have 3 arguments
  if(args.Length() == 3 || args.Length() == 4) {
    // Local<Boolean> asBuffer = args[2]->ToBoolean();    
    Buffer *buffer = Buffer::New(serialized_object, object_size);
    // Release the serialized string
    free(serialized_object);
    return scope.Close(buffer->handle_);
  } else {
    // Encode the string (string - null termiating character)
    Local<Value> bin_value = Encode(serialized_object, object_size, BINARY)->ToString();
    // Return the serialized content
    return bin_value;    
  }  
}

Handle<Value> BSON::CalculateObjectSize(const Arguments &args) {
  HandleScope scope;
  // Ensure we have a valid object
  if(args.Length() == 1 && !args[0]->IsObject()) return VException("One argument required - [object]");
  if(args.Length() == 2 && !args[0]->IsObject() && !args[1]->IsBoolean())  return VException("Two arguments required - [object, boolean]");
  if(args.Length() > 3) return VException("One or two arguments required - [object] or [object, boolean]");
  
  // Object size
  uint32_t object_size = 0;
  // Check if we have our argument, calculate size of the object  
  if(args.Length() == 2) {
    object_size = BSON::calculate_object_size(args[0], args[1]->BooleanValue());
  } else {
    object_size = BSON::calculate_object_size(args[0], false);
  }

  // Return the object size
  return scope.Close(Uint32::New(object_size));
}


Handle<Value> BSON::ToLong(const Arguments &args) {
  HandleScope scope;

  if(args.Length() != 2 && !args[0]->IsString() && !args[1]->IsString()) return VException("Two arguments of type String required");
  // Create a new Long value and return it
  Local<Value> argv[] = {args[0], args[1]};
  Handle<Value> long_obj = Long::constructor_template->GetFunction()->NewInstance(2, argv);    
  return scope.Close(long_obj);
}

Handle<Value> BSON::ToInt(const Arguments &args) {
  HandleScope scope;

  if(args.Length() != 1 && !args[0]->IsNumber() && !args[1]->IsString()) return VException("One argument of type Number required");  
  // Return int value
  return scope.Close(args[0]->ToInt32());
}

Handle<Value> BSON::EncodeLong(const Arguments &args) {
  HandleScope scope;
  
  // Encode the value
  if(args.Length() != 1 && !Long::HasInstance(args[0])) return VException("One argument required of type Long");
  // Unpack the object and encode
  Local<Object> obj = args[0]->ToObject();
  Long *long_obj = Long::Unwrap<Long>(obj);
  // Allocate space
  char *long_str = (char *)malloc(8 * sizeof(char));
  // Write the content to the char array
  BSON::write_int32((long_str), long_obj->low_bits);
  BSON::write_int32((long_str + 4), long_obj->high_bits);
  // Encode the data
  Local<String> long_final_str = Encode(long_str, 8, BINARY)->ToString();
  // Free up memory
  free(long_str);
  // Return the encoded string
  return scope.Close(long_final_str);
}

void BSON::write_int32(char *data, uint32_t value) {
  // Write the int to the char*
  memcpy(data, &value, 4);  
}

void BSON::write_double(char *data, double value) {
  // Write the double to the char*
  memcpy(data, &value, 8);    
}

void BSON::write_int64(char *data, int64_t value) {
  // Write the int to the char*
  memcpy(data, &value, 8);      
}

char *BSON::check_key(Local<String> key) {
  // Allocate space for they key string
  char *key_str = (char *)malloc(key->Utf8Length() * sizeof(char) + 1);
  // Error string
  char *error_str = (char *)malloc(256 * sizeof(char));
  // Decode the key
  ssize_t len = DecodeBytes(key, BINARY);
  ssize_t written = DecodeWrite(key_str, len, key, BINARY);
  *(key_str + key->Utf8Length()) = '\0';
  // Check if we have a valid key
  if(key->Utf8Length() > 0 && *(key_str) == '$') {
    // Create the string
    sprintf(error_str, "key %s must not start with '$'", key_str);
    // Free up memory
    free(key_str);
    // Throw exception with string
    throw error_str;
  } else if(key->Utf8Length() > 0 && strchr(key_str, '.') != NULL) {
    // Create the string
    sprintf(error_str, "key %s must not contain '.'", key_str);
    // Free up memory
    free(key_str);
    // Throw exception with string
    throw error_str;
  }
  // Free allocated space
  free(key_str);
  free(error_str);
  // Return No check key error
  return NULL;
}

uint32_t BSON::serialize(char *serialized_object, uint32_t index, Handle<Value> name, Handle<Value> value, bool check_key, bool serializeFunctions) {
  // Scope for method execution
  HandleScope scope;

  // If we have a name check that key is valid
  if(!name->IsNull() && check_key) {
    if(BSON::check_key(name->ToString()) != NULL) return -1;
  }  
  
  // Handle holder
  Local<String> constructorString;
  // Just check if we have an object
  bool isObject = value->IsObject();
  if(isObject) {
    constructorString = value->ToObject()->GetConstructorName();
  }
    
  // If we have an object let's serialize it  
  if(Long::HasInstance(value)) {
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_LONG;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;

    // Unpack the object and encode
    Local<Object> obj = value->ToObject();
    Long *long_obj = Long::Unwrap<Long>(obj);
    // Write the content to the char array
    BSON::write_int32((serialized_object + index), long_obj->low_bits);
    BSON::write_int32((serialized_object + index + 4), long_obj->high_bits);
    // Adjust the index
    index = index + 8;      
  } else if(Timestamp::HasInstance(value)) {
    // Point to start of data string
    uint32_t startIndex = index;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;
    
    // Save the string at the offset provided
    *(serialized_object + startIndex) = BSON_DATA_TIMESTAMP;
    // Unpack the object and encode
    Local<Object> obj = value->ToObject();
    Timestamp *timestamp_obj = Timestamp::Unwrap<Timestamp>(obj);
    // Write the content to the char array
    BSON::write_int32((serialized_object + index), timestamp_obj->low_bits);
    BSON::write_int32((serialized_object + index + 4), timestamp_obj->high_bits);
    // Adjust the index
    index = index + 8;
  } else if(ObjectID::HasInstance(value)) {
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_OID;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;    

    // Unpack the object and encode
    Local<Object> obj = value->ToObject();
    ObjectID *object_id_obj = ObjectID::Unwrap<ObjectID>(obj);
    // Fetch the converted oid
    char *binary_oid = object_id_obj->convert_hex_oid_to_bin();
    // Write the oid to the char array
    memcpy((serialized_object + index), binary_oid, 12);
    // Free memory
    free(binary_oid);
    // Adjust the index
    index = index + 12;          
  } else if(Binary::HasInstance(value)) { // || (value->IsObject() && value->ToObject()->GetConstructorName()->Equals(String::New("Binary")))) {
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_BINARY;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;    

    // Unpack the object and encode
    Local<Object> obj = value->ToObject();
    Binary *binary_obj = Binary::Unwrap<Binary>(obj);
    // Let's write the content to the char* array
    BSON::write_int32((serialized_object + index), binary_obj->index);
    // Adjust index
    index = index + 4;
    // Write subtype
    *(serialized_object + index)  = (char)binary_obj->sub_type;
    // Adjust index
    index = index + 1;
    // Write binary content
    memcpy((serialized_object + index), binary_obj->data, binary_obj->index);
    // Adjust index.rar">_</a>
    index = index + binary_obj->index;      
  } else if(DBRef::HasInstance(value)) { // || (value->IsObject() && value->ToObject()->GetConstructorName()->Equals(String::New("exports.DBRef")))) {
    // Unpack the dbref
    Local<Object> dbref = value->ToObject();
    // Create an object containing the right namespace variables
    Local<Object> obj = Object::New();
    // unpack dbref to get to the bin
    DBRef *db_ref_obj = DBRef::Unwrap<DBRef>(dbref);
    // Unpack the reference value
    Persistent<Value> oid_value = db_ref_obj->oid;
    // Encode the oid to bin
    obj->Set(String::New("$ref"), dbref->Get(String::New("namespace")));
    obj->Set(String::New("$id"), oid_value);      
    // obj->Set(String::New("$db"), dbref->Get(String::New("db")));
    if(db_ref_obj->db != NULL) obj->Set(String::New("$db"), dbref->Get(String::New("db")));
    // Encode the variable
    index = BSON::serialize(serialized_object, index, name, obj, false, serializeFunctions);
  } else if(Code::HasInstance(value)) { // || (value->IsObject() && value->ToObject()->GetConstructorName()->Equals(String::New("exports.Code")))) {
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_CODE_W_SCOPE;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;    

    // Unpack the object and encode
    Local<Object> obj = value->ToObject();
    Code *code_obj = Code::Unwrap<Code>(obj);
    // Keep pointer to start
    uint32_t first_pointer = index;
    // Adjust the index
    index = index + 4;
    // Write the size of the code string
    BSON::write_int32((serialized_object + index), strlen(code_obj->code) + 1);
    // Adjust the index
    index = index + 4;    
    // Write the code string
    memcpy((serialized_object + index), code_obj->code, strlen(code_obj->code));
    *(serialized_object + index + strlen(code_obj->code)) = '\0';
    // Adjust index
    index = index + strlen(code_obj->code) + 1;
    // Encode the scope
    uint32_t scope_object_size = BSON::calculate_object_size(code_obj->scope_object, serializeFunctions);
    // Serialize the scope object
    BSON::serialize((serialized_object + index), 0, Null(), code_obj->scope_object, check_key, serializeFunctions);
    // Adjust the index
    index = index + scope_object_size;
    // Encode the total size of the object
    BSON::write_int32((serialized_object + first_pointer), (index - first_pointer));
  } else if(Double::HasInstance(value)) {
    // printf("=================================================================== serialize double\n");
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_NUMBER;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;    

    // Unpack the double
    Local<Object> doubleHObject = value->ToObject();
    Double *double_obj = Double::Unwrap<Double>(doubleHObject);
    
    // Write the value out
    Local<Number> number = double_obj->value->ToNumber();
    // Get the values
    double d_number = number->NumberValue();
    
    // Write the double to the char array
    BSON::write_double((serialized_object + index), d_number);
    // Adjust index for double
    index = index + 8;    
  } else if(Symbol::HasInstance(value)) { // || (value->IsObject() && value->ToObject()->GetConstructorName()->Equals(String::New("exports.Symbol")))) {
    // Unpack the symbol
    Local<Object> symbol = value->ToObject();
    Symbol *symbol_obj = Symbol::Unwrap<Symbol>(symbol);
    // Let's get the length
    value = symbol_obj->value->ToString();    
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_SYMBOL;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;        
    
    // Write the actual string into the char array
    Local<String> str = value->ToString();
    // Let's fetch the int value
    uint32_t utf8_length = str->Utf8Length();

    // If the Utf8 length is different from the string length then we
    // have a UTF8 encoded string, otherwise write it as ascii
    if(utf8_length != str->Length()) {
      // Write the integer to the char *
      BSON::write_int32((serialized_object + index), utf8_length + 1);
      // Adjust the index
      index = index + 4;
      // Write string to char in utf8 format
      str->WriteUtf8((serialized_object + index), utf8_length);
      // Add the null termination
      *(serialized_object + index + utf8_length) = '\0';    
      // Adjust the index
      index = index + utf8_length + 1;      
    } else {
      // Write the integer to the char *
      BSON::write_int32((serialized_object + index), str->Length() + 1);
      // Adjust the index
      index = index + 4;
      // Write string to char in utf8 format
      written = DecodeWrite((serialized_object + index), str->Length(), str, BINARY);
      // Add the null termination
      *(serialized_object + index + str->Length()) = '\0';    
      // Adjust the index
      index = index + str->Length() + 1;      
    }       
  } else if(value->IsString()) {
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_STRING;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;        
    
    // Write the actual string into the char array
    Local<String> str = value->ToString();
    // Let's fetch the int value
    uint32_t utf8_length = str->Utf8Length();

    // If the Utf8 length is different from the string length then we
    // have a UTF8 encoded string, otherwise write it as ascii
    if(utf8_length != str->Length()) {
      // Write the integer to the char *
      BSON::write_int32((serialized_object + index), utf8_length + 1);
      // Adjust the index
      index = index + 4;
      // Write string to char in utf8 format
      str->WriteUtf8((serialized_object + index), utf8_length);
      // Add the null termination
      *(serialized_object + index + utf8_length) = '\0';    
      // Adjust the index
      index = index + utf8_length + 1;      
    } else {
      // Write the integer to the char *
      BSON::write_int32((serialized_object + index), str->Length() + 1);
      // Adjust the index
      index = index + 4;
      // Write string to char in utf8 format
      written = DecodeWrite((serialized_object + index), str->Length(), str, BINARY);
      // Add the null termination
      *(serialized_object + index + str->Length()) = '\0';    
      // Adjust the index
      index = index + str->Length() + 1;      
    }   
  } else if(MinKey::HasInstance(value)) {
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_MIN_KEY;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;    
  } else if(MaxKey::HasInstance(value)) {
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_MAX_KEY;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;    
  } else if(value->IsNull() || value->IsUndefined()) {
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_NULL;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;    
  } else if(value->IsNumber()) {
    uint32_t first_pointer = index;
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_INT;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;    
    
    Local<Number> number = value->ToNumber();
    // Get the values
    double d_number = number->NumberValue();
    int64_t l_number = number->IntegerValue();
    
    // Check if we have a double value and not a int64
    double d_result = d_number - l_number;    
    // If we have a value after subtracting the integer value we have a float
    if(d_result > 0 || d_result < 0) {
      // Write the double to the char array
      BSON::write_double((serialized_object + index), d_number);
      // Adjust type to be double
      *(serialized_object + first_pointer) = BSON_DATA_NUMBER;
      // Adjust index for double
      index = index + 8;
    } else if(l_number <= BSON_INT32_MAX && l_number >= BSON_INT32_MIN) {
      // printf("--------------------------------------------------------------- 2\n");
      // Smaller than 32 bit, write as 32 bit value
      BSON::write_int32(serialized_object + index, value->ToInt32()->Value());
      // Adjust the size of the index
      index = index + 4;
    } else if(l_number <= (2^53) && l_number >= (-2^53)) {
      // printf("--------------------------------------------------------------- 3\n");
      // Write the double to the char array
      BSON::write_double((serialized_object + index), d_number);
      // Adjust type to be double
      *(serialized_object + first_pointer) = BSON_DATA_NUMBER;
      // Adjust index for double
      index = index + 8;      
    } else {
      // printf("--------------------------------------------------------------- 4\n");
      BSON::write_double((serialized_object + index), d_number);
      // BSON::write_int64((serialized_object + index), d_number);
      // BSON::write_int64((serialized_object + index), l_number);
      // Adjust type to be double
      *(serialized_object + first_pointer) = BSON_DATA_NUMBER;
      // Adjust the size of the index
      index = index + 8;
    }     
  } else if(value->IsBoolean()) {
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_BOOLEAN;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;    

    // Save the boolean value
    *(serialized_object + index) = value->BooleanValue() ? '\1' : '\0';
    // Adjust the index
    index = index + 1;
  } else if(value->IsDate()) {
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_DATE;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;    

    // Fetch the Integer value
    int64_t integer_value = value->IntegerValue();
    BSON::write_int64((serialized_object + index), integer_value);
    // Adjust the index
    index = index + 8;
  // } else if(value->IsObject() && value->ToObject()->ObjectProtoToString()->Equals(String::New("[object RegExp]"))) {
  } else if(value->IsRegExp()) {
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_REGEXP;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;    

    // Fetch the string for the regexp
    Handle<RegExp> regExp = Handle<RegExp>::Cast(value);    
    len = DecodeBytes(regExp->GetSource(), UTF8);
    written = DecodeWrite((serialized_object + index), len, regExp->GetSource(), UTF8);
    int flags = regExp->GetFlags();
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;
    
    // ignorecase
    if((flags & (1 << 1)) != 0) {
      *(serialized_object + index) = 'i';
      index = index + 1;
    }
    
    //multiline
    if((flags & (1 << 2)) != 0) {
      *(serialized_object + index) = 'm';      
      index = index + 1;
    }
    
    // Add null termiation for the string
    *(serialized_object + index) = '\0';    
    // Adjust the index
    index = index + 1;
  } else if(value->IsArray()) {
    // Cast to array
    Local<Array> array = Local<Array>::Cast(value->ToObject());
    // Turn length into string to calculate the size of all the strings needed
    char *length_str = (char *)malloc(256 * sizeof(char));    
    // Save the string at the offset provided
    *(serialized_object + index) = BSON_DATA_ARRAY;
    // Adjust writing position for the first byte
    index = index + 1;
    // Convert name to char*
    ssize_t len = DecodeBytes(name, UTF8);
    ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
    // Add null termiation for the string
    *(serialized_object + index + len) = '\0';    
    // Adjust the index
    index = index + len + 1;        
    // Object size
    uint32_t object_size = BSON::calculate_object_size(value, serializeFunctions);
    // Write the size of the object
    BSON::write_int32((serialized_object + index), object_size);
    // Adjust the index
    index = index + 4;
    // Write out all the elements
    for(uint32_t i = 0; i < array->Length(); i++) {
      // Add "index" string size for each element
      sprintf(length_str, "%d", i);
      // Encode the values      
      index = BSON::serialize(serialized_object, index, String::New(length_str), array->Get(Integer::New(i)), check_key, serializeFunctions);
      // Write trailing '\0' for object
      *(serialized_object + index) = '\0';
    }

    // Pad the last item
    *(serialized_object + index) = '\0';
    index = index + 1;
    // Free up memory
    free(length_str);
  } else if(value->IsFunction()) {
    if(serializeFunctions) {
      // Save the string at the offset provided
      *(serialized_object + index) = BSON_DATA_CODE;
  
      // Adjust writing position for the first byte
      index = index + 1;
      // Convert name to char*
      ssize_t len = DecodeBytes(name, UTF8);
      ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
      // Add null termiation for the string
      *(serialized_object + index + len) = '\0';    
      // Adjust the index
      index = index + len + 1;    
  
      // Need to convert function into string
      Local<String> str = value->ToString();
      uint32_t utf8_length = str->Utf8Length();
  
      // If the Utf8 length is different from the string length then we
      // have a UTF8 encoded string, otherwise write it as ascii
      if(utf8_length != str->Length()) {
        // Write the integer to the char *
        BSON::write_int32((serialized_object + index), utf8_length + 1);
        // Adjust the index
        index = index + 4;
        // Write string to char in utf8 format
        str->WriteUtf8((serialized_object + index), utf8_length);
        // Add the null termination
        *(serialized_object + index + utf8_length) = '\0';    
        // Adjust the index
        index = index + utf8_length + 1;      
      } else {
        // Write the integer to the char *
        BSON::write_int32((serialized_object + index), str->Length() + 1);
        // Adjust the index
        index = index + 4;
        // Write string to char in utf8 format
        written = DecodeWrite((serialized_object + index), str->Length(), str, BINARY);
        // Add the null termination
        *(serialized_object + index + str->Length()) = '\0';    
        // Adjust the index
        index = index + str->Length() + 1;      
      }             
    }
  } else if(value->IsObject() && 
     (constructorString->Equals(String::New("exports.Long"))
     || constructorString->Equals(String::New("exports.Timestamp"))
     || (value->ToObject()->HasRealNamedProperty(String::New("toHexString")) || constructorString->Equals(String::New("ObjectID")))
     || constructorString->Equals(String::New("Binary"))
     || constructorString->Equals(String::New("exports.DBRef"))
     || constructorString->Equals(String::New("exports.Code"))
     || constructorString->Equals(String::New("exports.Double"))
     || constructorString->Equals(String::New("exports.MinKey"))
     || constructorString->Equals(String::New("exports.MaxKey"))
     || constructorString->Equals(String::New("exports.Symbol")))) {
    
    // Throw an error due to wrong class
    char *error_str = (char *)malloc(256 * sizeof(char));
    // Unpack the string for the type
    Local<String> constructorName = value->ToObject()->GetConstructorName();
    ssize_t objlen = DecodeBytes(constructorName, UTF8);
    char *cName = (char *)malloc(objlen * sizeof(char));
    ssize_t written = DecodeWrite(cName, objlen, constructorName, UTF8);
    *(cName + objlen) = '\0';
    
    // Create the error string    
    sprintf(error_str, "BSON Specific classes must be instances of the C++ versions not the JS versions when using the native bson parser, [%s]", cName);
    // free memory
    free(cName);
    // Throw exception with string
    throw error_str;
  } else if(value->IsObject()) {
    if(!name->IsNull()) {
      // Save the string at the offset provided
      *(serialized_object + index) = BSON_DATA_OBJECT;
      // Adjust writing position for the first byte
      index = index + 1;
      // Convert name to char*
      ssize_t len = DecodeBytes(name, UTF8);
      ssize_t written = DecodeWrite((serialized_object + index), len, name, UTF8);
      // Add null termiation for the string
      *(serialized_object + index + len) = '\0';    
      // Adjust the index
      index = index + len + 1;          
    }
        
    // Unwrap the object
    Local<Object> object = value->ToObject();
    Local<Array> property_names = object->GetOwnPropertyNames();

    // Calculate size of the total object
    uint32_t object_size = BSON::calculate_object_size(value, serializeFunctions);
    // Write the size
    BSON::write_int32((serialized_object + index), object_size);
    // Adjust size
    index = index + 4;    
    
    // Process all the properties on the object
    for(uint32_t i = 0; i < property_names->Length(); i++) {
      // Fetch the property name
      Local<String> property_name = property_names->Get(i)->ToString();      
      // Fetch the object for the property
      Local<Value> property = object->Get(property_name);
      // Write the next serialized object
      // printf("========== !property->IsFunction() || (property->IsFunction() && serializeFunctions) = %d\n", !property->IsFunction() || (property->IsFunction() && serializeFunctions) == true ? 1 : 0);
      if(!property->IsFunction() || (property->IsFunction() && serializeFunctions)) {
        // Convert name to char*
        ssize_t len = DecodeBytes(property_name, UTF8);
        // char *data = new char[len];
        char *data = (char *)malloc(len + 1);
        *(data + len) = '\0';
        ssize_t written = DecodeWrite(data, len, property_name, UTF8);      
        // Serialize the content
        index = BSON::serialize(serialized_object, index, property_name, property, check_key, serializeFunctions);      
        // Free up memory of data
        free(data);
      }
    }
    // Pad the last item
    *(serialized_object + index) = '\0';
    index = index + 1;

    // Null out reminding fields if we have a toplevel object and nested levels
    if(name->IsNull()) {
      for(uint32_t i = 0; i < (object_size - index); i++) {
        *(serialized_object + index + i) = '\0';
      }
    }    
  }
  
  return index;
}

uint32_t BSON::calculate_object_size(Handle<Value> value, bool serializeFunctions) {
  uint32_t object_size = 0;

  // Handle holder
  Local<String> constructorString;
  // Just check if we have an object
  bool isObject = value->IsObject();
  if(isObject) {
    constructorString = value->ToObject()->GetConstructorName();
  }

  // If we have an object let's unwrap it and calculate the sub sections
  if(Long::HasInstance(value)) {
    object_size = object_size + 8;
  } else if(Timestamp::HasInstance(value)) {
    object_size = object_size + 8;
  } else if(ObjectID::HasInstance(value)) {
    object_size = object_size + 12;
  } else if(Binary::HasInstance(value)) {
    // Unpack the object and encode
    Local<Object> obj = value->ToObject();
    Binary *binary_obj = Binary::Unwrap<Binary>(obj);
    // Adjust the object_size, binary content lengt + total size int32 + binary size int32 + subtype
    object_size += binary_obj->index + 4 + 1;
  } else if(Code::HasInstance(value)) {
    // Unpack the object and encode
    Local<Object> obj = value->ToObject();
    Code *code_obj = Code::Unwrap<Code>(obj);
    // Let's calculate the size the code object adds adds
    object_size += strlen(code_obj->code) + 4 + BSON::calculate_object_size(code_obj->scope_object, serializeFunctions) + 4 + 1;
  } else if(DBRef::HasInstance(value)) {
    // Unpack the dbref
    Local<Object> dbref = value->ToObject();
    // Create an object containing the right namespace variables
    Local<Object> obj = Object::New();
    // unpack dbref to get to the bin
    DBRef *db_ref_obj = DBRef::Unwrap<DBRef>(dbref);
    // Encode the oid to bin
    obj->Set(String::New("$ref"), dbref->Get(String::New("namespace")));
    obj->Set(String::New("$id"), db_ref_obj->oid);
    // obj->Set(String::New("$db"), dbref->Get(String::New("db")));
    if(db_ref_obj->db != NULL) obj->Set(String::New("$db"), dbref->Get(String::New("db")));
    // Calculate size
    object_size += BSON::calculate_object_size(obj, serializeFunctions);
  } else if(MinKey::HasInstance(value) || MaxKey::HasInstance(value)) {    
  } else if(Symbol::HasInstance(value)) {
    // Unpack the dbref
    Local<Object> dbref = value->ToObject();
    // unpack dbref to get to the bin
    Symbol *symbol_obj = Symbol::Unwrap<Symbol>(dbref);
    // Let's get the length
    Local<String> str = symbol_obj->value->ToString();
    uint32_t utf8_length = str->Utf8Length();
    
    if(utf8_length != str->Length()) {
      // Let's calculate the size the string adds, length + type(1 byte) + size(4 bytes)
      object_size += str->Utf8Length() + 1 + 4;  
    } else {
      object_size += str->Length() + 1 + 4;        
    }    
  } else if(value->IsString()) {
    Local<String> str = value->ToString();
    uint32_t utf8_length = str->Utf8Length();
    
    if(utf8_length != str->Length()) {
      // Let's calculate the size the string adds, length + type(1 byte) + size(4 bytes)
      object_size += str->Utf8Length() + 1 + 4;  
    } else {
      object_size += str->Length() + 1 + 4;        
    }
  } else if(value->IsNull()) {
  } else if(Double::HasInstance(value)) {
    object_size = object_size + 8;
  } else if(value->IsNumber()) {
    // Check if we have a float value or a long value
    Local<Number> number = value->ToNumber();
    double d_number = number->NumberValue();
    int64_t l_number = number->IntegerValue();
    // Check if we have a double value and not a int64
    double d_result = d_number - l_number;    
    // If we have a value after subtracting the integer value we have a float
    if(d_result > 0 || d_result < 0) {
      object_size = object_size + 8;      
    } else if(l_number <= BSON_INT32_MAX && l_number >= BSON_INT32_MIN) {
      object_size = object_size + 4;
    } else {
      object_size = object_size + 8;
    }
  } else if(value->IsBoolean()) {
    object_size = object_size + 1;
  } else if(value->IsDate()) {
    object_size = object_size + 8;
  } else if(value->IsRegExp()) {
    // Fetch the string for the regexp
    Handle<RegExp> regExp = Handle<RegExp>::Cast(value);    
    ssize_t len = DecodeBytes(regExp->GetSource(), UTF8);
    int flags = regExp->GetFlags();
    
    // ignorecase
    if((flags & (1 << 1)) != 0) len++;
    //multiline
    if((flags & (1 << 2)) != 0) len++;
    // Calculate the space needed for the regexp: size of string - 2 for the /'ses +2 for null termiations
    object_size = object_size + len + 2;
  } else if(value->IsArray()) {
    // Cast to array
    Local<Array> array = Local<Array>::Cast(value->ToObject());
    // Turn length into string to calculate the size of all the strings needed
    char *length_str = (char *)malloc(256 * sizeof(char));
    // Calculate the size of each element
    for(uint32_t i = 0; i < array->Length(); i++) {
      // Add "index" string size for each element
      sprintf(length_str, "%d", i);
      // Add the size of the string length
      uint32_t label_length = strlen(length_str) + 1;
      // Add the type definition size for each item
      object_size = object_size + label_length + 1;
      // Add size of the object
      uint32_t object_length = BSON::calculate_object_size(array->Get(Integer::New(i)), serializeFunctions);
      object_size = object_size + object_length;
    }
    // Add the object size
    object_size = object_size + 4 + 1;
    // Free up memory
    free(length_str);
  } else if(value->IsFunction()) {
    // printf("========== value->IsFunction() && serializeFunctions :: %d\n", value->IsFunction() && serializeFunctions == true ? 1 : 0);
    if(serializeFunctions) {
      // Need to convert function into string
      Local<String> functionString = value->ToString();
      // Length of string
      ssize_t len = DecodeBytes(functionString, UTF8);
      // Adjust size of binary
      object_size += len + 1 + 4;
    }
  } else if(value->IsObject() && 
     (constructorString->Equals(String::New("exports.Long"))
     || constructorString->Equals(String::New("exports.Timestamp"))
     || (value->ToObject()->HasRealNamedProperty(String::New("toHexString")) || constructorString->Equals(String::New("ObjectID")))
     || constructorString->Equals(String::New("Binary"))
     || constructorString->Equals(String::New("exports.DBRef"))
     || constructorString->Equals(String::New("exports.Code"))
     || constructorString->Equals(String::New("exports.Double"))
     || constructorString->Equals(String::New("exports.MinKey"))
     || constructorString->Equals(String::New("exports.MaxKey"))
     || constructorString->Equals(String::New("exports.Symbol")))) {

    // Throw an error due to wrong class
    char *error_str = (char *)malloc(256 * sizeof(char));
    // Unpack the string for the type
    Local<String> constructorName = value->ToObject()->GetConstructorName();
    ssize_t objlen = DecodeBytes(constructorName, UTF8);
    char *cName = (char *)malloc(objlen * sizeof(char));
    ssize_t written = DecodeWrite(cName, objlen, constructorName, UTF8);
    *(cName + objlen) = '\0';

    // Create the error string    
    sprintf(error_str, "BSON Specific classes must be instances of the C++ versions not the JS versions when using the native bson parser, [%s]", cName);
    // free memory
    free(cName);
    // Throw exception with string
    throw error_str;
  } else if(value->IsObject()) {
    // Unwrap the object
    Local<Object> object = value->ToObject();
    Local<Array> property_names = object->GetOwnPropertyNames();
    
    // Process all the properties on the object
    for(uint32_t index = 0; index < property_names->Length(); index++) {
      // Fetch the property name
      Local<String> property_name = property_names->Get(index)->ToString();
      
      // Fetch the object for the property
      Local<Value> property = object->Get(property_name);
      // Get size of property (property + property name length + 1 for terminating 0)
      // printf("========== 1111111111111 !property->IsFunction() || (property->IsFunction() && serializeFunctions) = %d\n", !property->IsFunction() || (property->IsFunction() && serializeFunctions) == true ? 1 : 0);
      if(!property->IsFunction() || (property->IsFunction() && serializeFunctions)) {
        // Convert name to char*
        ssize_t len = DecodeBytes(property_name, UTF8);
        object_size += BSON::calculate_object_size(property, serializeFunctions) + len + 1 + 1;
      }
    }      
    
    object_size = object_size + 4 + 1;
  } 

  return object_size;
}

Handle<Value> BSON::BSONDeserialize(const Arguments &args) {
  HandleScope scope;

  // Ensure that we have an parameter
  if(Buffer::HasInstance(args[0]) && args.Length() > 1) return VException("One argument required - buffer1.");
  if(args[0]->IsString() && args.Length() > 1) return VException("One argument required - string1.");
  // Throw an exception if the argument is not of type Buffer
  if(!Buffer::HasInstance(args[0]) && !args[0]->IsString()) return VException("Argument must be a Buffer or String.");
  
  // Define pointer to data
  char *data;
  uint32_t length;      
  Local<Object> obj = args[0]->ToObject();

  // If we passed in a buffer, let's unpack it, otherwise let's unpack the string
  if(Buffer::HasInstance(obj)) {

    #if NODE_MAJOR_VERSION == 0 && NODE_MINOR_VERSION < 3
     Buffer *buffer = ObjectWrap::Unwrap<Buffer>(obj);
     data = buffer->data();
     uint32_t length = buffer->length();
    #else
     data = Buffer::Data(obj);
     uint32_t length = Buffer::Length(obj);
    #endif

    return BSON::deserialize(data, NULL);
  } else {
    // Let's fetch the encoding
    // enum encoding enc = ParseEncoding(args[1]);
    // The length of the data for this encoding
    ssize_t len = DecodeBytes(args[0], BINARY);
    // Let's define the buffer size
    // data = new char[len];
    data = (char *)malloc(len);
    // Write the data to the buffer from the string object
    ssize_t written = DecodeWrite(data, len, args[0], BINARY);
    // Assert that we wrote the same number of bytes as we have length
    assert(written == len);
    // Get result
    Handle<Value> result = BSON::deserialize(data, NULL);
    // Free memory
    free(data);
    // Deserialize the content
    return result;
  }  
}

// Deserialize the stream
Handle<Value> BSON::deserialize(char *data, bool is_array_item) {
  HandleScope scope;
  // Holds references to the objects that are going to be returned
  Local<Object> return_data = Object::New();
  Local<Array> return_array = Array::New();      
  // The current index in the char data
  uint32_t index = 0;
  // Decode the size of the BSON data structure
  uint32_t size = BSON::deserialize_int32(data, index);
  // Adjust the index to point to next piece
  index = index + 4;      

  // While we have data left let's decode
  while(index < size) {
    // Read the first to bytes to indicate the type of object we are decoding
    uint8_t type = BSON::deserialize_int8(data, index);    
    // Handles the internal size of the object
    uint32_t insert_index = 0;
    // Adjust index to skip type byte
    index = index + 1;
    
    if(type == BSON_DATA_STRING) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      

      // Read the length of the string (next 4 bytes)
      uint32_t string_size = BSON::deserialize_int32(data, index);
      // Adjust index to point to start of string
      index = index + 4;
      // Decode the string and add zero terminating value at the end of the string
      char *value = (char *)malloc((string_size * sizeof(char)));
      strncpy(value, (data + index), string_size);
      // Encode the string (string - null termiating character)
      Local<Value> utf8_encoded_str = Encode(value, string_size - 1, UTF8)->ToString();
      // Add the value to the data
      if(is_array_item) {
        return_array->Set(Number::New(insert_index), utf8_encoded_str);
      } else {
        return_data->Set(String::New(string_name), utf8_encoded_str);
      }
      
      // Adjust index
      index = index + string_size;
      // Free up the memory
      free(value);
      free(string_name);
    } else if(type == BSON_DATA_INT) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      
      
      // Decode the integer value
      uint32_t value = 0;
      memcpy(&value, (data + index), 4);
            
      // Adjust the index for the size of the value
      index = index + 4;
      // Add the element to the object
      if(is_array_item) {
        return_array->Set(Integer::New(insert_index), Integer::New(value));
      } else {
        return_data->Set(String::New(string_name), Integer::New(value));
      }          
      // Free up the memory
      free(string_name);
    } else if(type == BSON_DATA_TIMESTAMP) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      
      
      // Decode the integer value
      int64_t value = 0;
      memcpy(&value, (data + index), 8);      
      // Adjust the index for the size of the value
      index = index + 8;
            
      // Add the element to the object
      if(is_array_item) {
        return_array->Set(Number::New(insert_index), BSON::decodeTimestamp(value));
      } else {
        return_data->Set(String::New(string_name), BSON::decodeTimestamp(value));
      }
      // Free up the memory
      free(string_name);            
    } else if(type == BSON_DATA_LONG) {
      // printf("=================================================== 1\n");      
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      
            
      // Add the element to the object
      if(is_array_item) {
        return_array->Set(Number::New(insert_index), BSON::decodeLong(data, index));
      } else {
        return_data->Set(String::New(string_name), BSON::decodeLong(data, index));
      }        

      // Adjust the index for the size of the value
      index = index + 8;

      // Free up the memory
      free(string_name);      
    } else if(type == BSON_DATA_NUMBER) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      
      
      // Decode the integer value
      double value = 0;
      memcpy(&value, (data + index), 8);      
      // Adjust the index for the size of the value
      index = index + 8;
      
      // Add the element to the object
      if(is_array_item) {
        return_array->Set(Number::New(insert_index), Number::New(value));
      } else {
        return_data->Set(String::New(string_name), Number::New(value));
      }
      // Free up the memory
      free(string_name);      
    } else if(type == BSON_DATA_MIN_KEY) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      

      // Create new MinKey
      MinKey *minKey = MinKey::New();      
      // Add the element to the object
      if(is_array_item) {
        return_array->Set(Number::New(insert_index), minKey->handle_);
      } else {
        return_data->Set(String::New(string_name), minKey->handle_);
      }      
      // Free up the memory
      free(string_name);      
    } else if(type == BSON_DATA_MAX_KEY) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      
      
      // Create new MinKey
      MaxKey *maxKey = MaxKey::New();      
      // Add the element to the object
      if(is_array_item) {
        return_array->Set(Number::New(insert_index), maxKey->handle_);
      } else {
        return_data->Set(String::New(string_name), maxKey->handle_);
      }      
      // Free up the memory
      free(string_name);      
    } else if(type == BSON_DATA_NULL) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      
      
      // Add the element to the object
      if(is_array_item) {
        return_array->Set(Number::New(insert_index), Null());
      } else {
        return_data->Set(String::New(string_name), Null());
      }      
      // Free up the memory
      free(string_name);      
    } else if(type == BSON_DATA_BOOLEAN) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      

      // Decode the boolean value
      char bool_value = *(data + index);
      // Adjust the index for the size of the value
      index = index + 1;
      
      // Add the element to the object
      if(is_array_item) {
        return_array->Set(Number::New(insert_index), bool_value == 1 ? Boolean::New(true) : Boolean::New(false));
      } else {
        return_data->Set(String::New(string_name), bool_value == 1 ? Boolean::New(true) : Boolean::New(false));
      }            
      // Free up the memory
      free(string_name);      
    } else if(type == BSON_DATA_DATE) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      

      // Decode the value 64 bit integer
      int64_t value = 0;
      memcpy(&value, (data + index), 8);      
      // Adjust the index for the size of the value
      index = index + 8;
      // Add the element to the object
      if(is_array_item) {
        return_array->Set(Number::New(insert_index), Date::New((double)value));
      } else {
        return_data->Set(String::New(string_name), Date::New((double)value));
      }     
      // Free up the memory
      free(string_name);        
    } else if(type == BSON_DATA_REGEXP) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      

      // Length variable
      int32_t length_regexp = 0;
      int32_t start_index = index;
      char chr;
      
      // Locate end of the regexp expression \0
      while((chr = *(data + index + length_regexp)) != '\0') {
        length_regexp = length_regexp + 1;
      }

      // Contains the reg exp
      char *reg_exp = (char *)malloc(length_regexp * sizeof(char) + 2);
      // Copy the regexp from the data to the char *
      memcpy(reg_exp, (data + index), (length_regexp + 1));
      // Adjust the index to skip the first part of the regular expression
      index = index + length_regexp + 1;
            
      // Reset the length
      int32_t options_length = 0;
      // Locate the end of the options for the regexp terminated with a '\0'
      while((chr = *(data + index + options_length)) != '\0') {
        options_length = options_length + 1;
      }

      // Contains the reg exp
      char *options = (char *)malloc(options_length * sizeof(char) + 1);
      // Copy the options from the data to the char *
      memcpy(options, (data + index), (options_length + 1));      
      // Adjust the index to skip the option part of the regular expression
      index = index + options_length + 1;      
      // ARRRRGH Google does not expose regular expressions through the v8 api
      // Have to use Script to instantiate the object (slower)

      // Generate the string for execution in the string context
      int flag = 0;

      for(int i = 0; i < options_length; i++) {
        // Multiline
        if(*(options + i) == 'm') {
          flag = flag | 4;
        } else if(*(options + i) == 'i') {
          flag = flag | 2;          
        }
      }

      // Add the element to the object
      if(is_array_item) {
        return_array->Set(Number::New(insert_index), RegExp::New(String::New(reg_exp), (v8::RegExp::Flags)flag));
      } else {
        return_data->Set(String::New(string_name), RegExp::New(String::New(reg_exp), (v8::RegExp::Flags)flag));
      }  
      
      // Free memory
      free(reg_exp);          
      free(options);          
      free(string_name);
    } else if(type == BSON_DATA_OID) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      
      
      // Allocate storage for a 24 character hex oid    
      char *oid_string = (char *)malloc(12 * 2 * sizeof(char) + 1);
      char *pbuffer = oid_string;      
      // Terminate the string
      *(pbuffer + 24) = '\0';      
      // Unpack the oid in hex form
      for(int32_t i = 0; i < 12; i++) {
        sprintf(pbuffer, "%02x", (unsigned char)*(data + index + i));
        pbuffer += 2;
      }      

      // Adjust the index
      index = index + 12;

      // Add the element to the object
      if(is_array_item) {
        return_array->Set(Number::New(insert_index), BSON::decodeOid(oid_string));
      } else {
        return_data->Set(String::New(string_name), BSON::decodeOid(oid_string));
      }     
      // Free memory
      free(oid_string);                       
      free(string_name);
    } else if(type == BSON_DATA_BINARY) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      
      
      // Read the binary data size
      uint32_t number_of_bytes = BSON::deserialize_int32(data, index);
      // Adjust the index
      index = index + 4;
      // Decode the subtype, ensure it's positive
      uint32_t sub_type = (int)*(data + index) & 0xff;
      // Adjust the index
      index = index + 1;
      // Copy the binary data into a buffer
      char *buffer = (char *)malloc(number_of_bytes * sizeof(char) + 1);
      memcpy(buffer, (data + index), number_of_bytes);
      *(buffer + number_of_bytes) = '\0';
      // Adjust the index
      index = index + number_of_bytes;
      // Add the element to the object
      if(is_array_item) {
        return_array->Set(Number::New(insert_index), BSON::decodeBinary(sub_type, number_of_bytes, buffer));
      } else {
        return_data->Set(String::New(string_name), BSON::decodeBinary(sub_type, number_of_bytes, buffer));
      }
      // Free memory
      free(buffer);                             
      free(string_name);
    } else if(type == BSON_DATA_SYMBOL) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      

      // Read the length of the string (next 4 bytes)
      uint32_t string_size = BSON::deserialize_int32(data, index);
      // Adjust index to point to start of string
      index = index + 4;
      // Decode the string and add zero terminating value at the end of the string
      char *value = (char *)malloc((string_size * sizeof(char)));
      strncpy(value, (data + index), string_size);
      // Encode the string (string - null termiating character)
      Local<Value> utf8_encoded_str = Encode(value, string_size - 1, UTF8)->ToString();
      
      // Wrap up the string in a Symbol Object
      Local<Value> argv[] = {utf8_encoded_str};
      Handle<Value> symbol_obj = Symbol::constructor_template->GetFunction()->NewInstance(1, argv);
      
      // Add the value to the data
      if(is_array_item) {
        return_array->Set(Number::New(insert_index), symbol_obj);
      } else {
        return_data->Set(String::New(string_name), symbol_obj);
      }
      
      // Adjust index
      index = index + string_size;
      // Free up the memory
      free(value);
      free(string_name);
    } else if(type == BSON_DATA_CODE) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      
      
      // Read the string size
      uint32_t string_size = BSON::deserialize_int32(data, index);
      // Adjust the index
      index = index + 4;
      // Read the string
      char *code = (char *)malloc(string_size * sizeof(char) + 1);
      // Copy string + terminating 0
      memcpy(code, (data + index), string_size);

      // Define empty scope object
      Handle<Value> scope_object = Object::New();

      // Define the try catch block
      TryCatch try_catch;                
      // Decode the code object
      Handle<Value> obj = BSON::decodeCode(code, scope_object);
      // If an error was thrown push it up the chain
      if(try_catch.HasCaught()) {
        free(string_name);
        free(code);
        // Rethrow exception
        return try_catch.ReThrow();
      }

      // Add the element to the object
      if(is_array_item) {        
        return_array->Set(Number::New(insert_index), obj);
      } else {
        return_data->Set(String::New(string_name), obj);
      }      
      // Clean up memory allocation
      free(code);
      free(string_name);
    } else if(type == BSON_DATA_CODE_W_SCOPE) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      
      
      // Total number of bytes after array index
      uint32_t total_code_size = BSON::deserialize_int32(data, index);
      // Adjust the index
      index = index + 4;
      // Read the string size
      uint32_t string_size = BSON::deserialize_int32(data, index);
      // Adjust the index
      index = index + 4;
      // Read the string
      char *code = (char *)malloc(string_size * sizeof(char) + 1);
      // Copy string + terminating 0
      memcpy(code, (data + index), string_size);
      // Adjust the index
      index = index + string_size;      
      // Get the scope object (bson object)
      uint32_t bson_object_size = total_code_size - string_size - 8;
      // Allocate bson object buffer and copy out the content
      char *bson_buffer = (char *)malloc(bson_object_size * sizeof(char));
      memcpy(bson_buffer, (data + index), bson_object_size);
      // Adjust the index
      index = index + bson_object_size;
      // Parse the bson object
      Handle<Value> scope_object = BSON::deserialize(bson_buffer, false);
      // Define the try catch block
      TryCatch try_catch;                
      // Decode the code object
      Handle<Value> obj = BSON::decodeCode(code, scope_object);
      // If an error was thrown push it up the chain
      if(try_catch.HasCaught()) {
        // Clean up memory allocation
        free(string_name);
        free(bson_buffer);
        free(code);
        // Rethrow exception
        return try_catch.ReThrow();
      }

      // Add the element to the object
      if(is_array_item) {        
        return_array->Set(Number::New(insert_index), obj);
      } else {
        return_data->Set(String::New(string_name), obj);
      }      
      // Clean up memory allocation
      free(code);
      free(bson_buffer);      
      free(string_name);
    } else if(type == BSON_DATA_OBJECT) {
      // If this is the top level object we need to skip the undecoding
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }             
      
      // Get the object size
      uint32_t bson_object_size = BSON::deserialize_int32(data, index);
      // Define the try catch block
      TryCatch try_catch;                
      // Decode the code object
      Handle<Value> obj = BSON::deserialize(data + index, false);
      // Adjust the index
      index = index + bson_object_size;
      // If an error was thrown push it up the chain
      if(try_catch.HasCaught()) {
        // Rethrow exception
        return try_catch.ReThrow();
      }
      
      // Add the element to the object
      if(is_array_item) {        
        return_array->Set(Number::New(insert_index), obj);
      } else {
        return_data->Set(String::New(string_name), obj);
      }
      
      // Clean up memory allocation
      free(string_name);
    } else if(type == BSON_DATA_ARRAY) {
      // Read the null terminated index String
      char *string_name = BSON::extract_string(data, index);
      if(string_name == NULL) return VException("Invalid C String found.");
      // Let's create a new string
      index = index + strlen(string_name) + 1;
      // Handle array value if applicable
      uint32_t insert_index = 0;
      if(is_array_item) {
        insert_index = atoi(string_name);
      }      
      
      // Get the size
      uint32_t array_size = BSON::deserialize_int32(data, index);
      // Define the try catch block
      TryCatch try_catch;                

      // Decode the code object
      Handle<Value> obj = BSON::deserialize(data + index, true);
      // If an error was thrown push it up the chain
      if(try_catch.HasCaught()) {
        // Rethrow exception
        return try_catch.ReThrow();
      }
      // Adjust the index for the next value
      index = index + array_size;
      // Add the element to the object
      if(is_array_item) {        
        return_array->Set(Number::New(insert_index), obj);
      } else {
        return_data->Set(String::New(string_name), obj);
      }      
      // Clean up memory allocation
      free(string_name);
    }
  }
  
  // Check if we have a db reference
  if(!is_array_item && return_data->Has(String::New("$ref")) && return_data->Has(String::New("$id"))) {
    Handle<Value> dbref_value;
    dbref_value = BSON::decodeDBref(return_data->Get(String::New("$ref")), return_data->Get(String::New("$id")), return_data->Get(String::New("$db")));
    return scope.Close(dbref_value);
  }
  
  // Return the data object to javascript
  if(is_array_item) {
    return scope.Close(return_array);
  } else {
    return scope.Close(return_data);
  }
}

const char* BSON::ToCString(const v8::String::Utf8Value& value) {
  return *value ? *value : "<string conversion failed>";
}

Handle<Value> BSON::decodeDBref(Local<Value> ref, Local<Value> oid, Local<Value> db) {
  HandleScope scope;
  Local<Value> argv[] = {ref, oid, db};
  Handle<Value> dbref_obj = DBRef::constructor_template->GetFunction()->NewInstance(3, argv);    
  return scope.Close(dbref_obj);
}

Handle<Value> BSON::decodeCode(char *code, Handle<Value> scope_object) {
  HandleScope scope;
  
  Local<Value> argv[] = {String::New(code), scope_object->ToObject()};
  Handle<Value> code_obj = Code::constructor_template->GetFunction()->NewInstance(2, argv);
  return scope.Close(code_obj);
}

Handle<Value> BSON::decodeBinary(uint32_t sub_type, uint32_t number_of_bytes, char *data) {
  HandleScope scope;

  Local<String> str = Encode(data, number_of_bytes, BINARY)->ToString();
  Local<Value> argv[] = {str, Integer::New(sub_type)};
  Handle<Value> binary_obj = Binary::constructor_template->GetFunction()->NewInstance(2, argv);
  return scope.Close(binary_obj);
}

Handle<Value> BSON::decodeOid(char *oid) {
  HandleScope scope;
  
  Local<Value> argv[] = {String::New(oid)};
  Handle<Value> oid_obj = ObjectID::constructor_template->GetFunction()->NewInstance(1, argv);
  return scope.Close(oid_obj);
}

Handle<Value> BSON::decodeLong(char *data, uint32_t index) {
  HandleScope scope;
  
  // Decode the integer value
  int32_t lowBits = 0;
  int32_t highBits = 0;
  memcpy(&lowBits, (data + index), 4);        
  memcpy(&highBits, (data + index + 4), 4);        
  
  // Decode 64bit value
  int64_t value = 0;
  memcpy(&value, (data + index), 8);        
  
  // printf("==================================== %llu\n", value);
  
  // if(value >= (-2^53) && value <= (2^53)) {
  //   printf("----------------------------------------------- 2\n");
  //   
  // }
  
  // If value is < 2^53 and >-2^53
  if((highBits < 0x200000 || (highBits == 0x200000 && lowBits == 0)) && highBits >= -0x200000) {
    // printf("----------------------------------------------- 1\n");
    int64_t finalValue = 0;
    memcpy(&finalValue, (data + index), 8);        
    return scope.Close(Number::New(finalValue));
  }

  // Local<Value> argv[] = {Number::New(value)};
  Local<Value> argv[] = {Int32::New(lowBits), Int32::New(highBits)};
  Handle<Value> long_obj = Long::constructor_template->GetFunction()->NewInstance(2, argv);
  return scope.Close(long_obj);      
}

Handle<Value> BSON::decodeTimestamp(int64_t value) {
  HandleScope scope;
  
  Local<Value> argv[] = {Number::New(value)};
  Handle<Value> timestamp_obj = Timestamp::constructor_template->GetFunction()->NewInstance(1, argv);    
  return scope.Close(timestamp_obj);      
}

// Search for 0 terminated C string and return the string
char* BSON::extract_string(char *data, uint32_t offset) {
  char *prt = strchr((data + offset), '\0');
  if(prt == NULL) return NULL;
  // Figure out the length of the string
  uint32_t length = (prt - data) - offset;      
  // Allocate memory for the new string
  char *string_name = (char *)malloc((length * sizeof(char)) + 1);
  // Copy the variable into the string_name
  strncpy(string_name, (data + offset), length);
  // Ensure the string is null terminated
  *(string_name + length) = '\0';
  // Return the unpacked string
  return string_name;
}

// Decode a signed byte
int BSON::deserialize_sint8(char *data, uint32_t offset) {
  return (signed char)(*(data + offset));
}

int BSON::deserialize_sint16(char *data, uint32_t offset) {
  return BSON::deserialize_sint8(data, offset) + (BSON::deserialize_sint8(data, offset + 1) << 8);
}

long BSON::deserialize_sint32(char *data, uint32_t offset) {
  return (long)BSON::deserialize_sint8(data, offset) + (BSON::deserialize_sint8(data, offset + 1) << 8) +
    (BSON::deserialize_sint8(data, offset + 2) << 16) + (BSON::deserialize_sint8(data, offset + 3) << 24);
}

// Convert raw binary string to utf8 encoded string
char *BSON::decode_utf8(char *string, uint32_t length) {  
  // Internal variables
  uint32_t i = 0;
  uint32_t utf8_i = 0;
  // unsigned char unicode = 0;
  uint16_t unicode = 0;
  unsigned char c = 0;
  unsigned char c1 = 0;
  unsigned char c2 = 0;
  unsigned char c3 = 0;
  // Allocate enough space for the utf8 encoded string
  char *utf8_string = (char*)malloc(length * sizeof(char));
  // Process the utf8 raw string
  while(i < length) {
    // Fetch character
    c = (unsigned char)string[i];

    if(c < 128) {
    //   // It's a basic ascii character just copy the string
      *(utf8_string + utf8_i) = *(string + i);
      // Upadate indexs
      i = i + 1;
      utf8_i = utf8_i + 1;
    } else if((c > 191) && (c < 224)) {
      // Let's create an integer containing the 16 bit value for unicode
      c2 = (unsigned char)string[i + 1];
      // Pack to unicode value
      unicode = (uint16_t)(((c & 31) << 6) | (c2 & 63));
      // Write the int 16 to the string and upate index
      memcpy((utf8_string + utf8_i), &unicode, 2);
      // Upadate index
      i = i + 2;
      utf8_i = utf8_i + 2;
    } else {
    //   // Let's create the integers containing the 16 bit value for unicode
      c2 = (unsigned char)string[i + 1];
      c3 = (unsigned char)string[i + 2];
      // Pack to unicode value
      unicode = (uint16_t)(((c & 15) << 12) | ((c2 & 63) << 6) | (c3 & 63));
      // Write the int 16 to the string and upate index
      memcpy((utf8_string + utf8_i), &unicode, 2);     
      // Upadate indexs
      i = i + 3;
      utf8_i = utf8_i + 2;
    }
  }
  // Add null termiating character
  *(utf8_string + utf8_i + 1) = '\0';
  // Return pointer of converted string
  return utf8_string;
}

// Decode a byte
uint16_t BSON::deserialize_int8(char *data, uint32_t offset) {
  uint16_t value = 0;
  value |= *(data + offset + 0);              
  return value;
}

// Requires a 4 byte char array
uint32_t BSON::deserialize_int32(char* data, uint32_t offset) {
  uint32_t value = 0;
  memcpy(&value, (data + offset), 4);
  return value;
}

// Exporting function
extern "C" void init(Handle<Object> target) {
  HandleScope scope;
  BSON::Initialize(target);
  Long::Initialize(target);
  ObjectID::Initialize(target);
  Binary::Initialize(target);
  Code::Initialize(target);
  DBRef::Initialize(target);
  Timestamp::Initialize(target);
  Symbol::Initialize(target);
  MinKey::Initialize(target);
  MaxKey::Initialize(target);
  Double::Initialize(target);
}

// NODE_MODULE(bson, BSON::Initialize);
// NODE_MODULE(l, Long::Initialize);
