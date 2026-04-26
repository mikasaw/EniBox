typedef LONG NTSTATUS;  
typedef struct _UNICODE_STRING_NT { USHORT Length; USHORT MaximumLength; PWSTR Buffer; } UNICODE_STRING_NT;  
typedef UNICODE_STRING_NT *PUNICODE_STRING_NT;  
typedef struct _OBJECT_ATTRIBUTES_NT { ULONG Length; HANDLE RootDirectory; PUNICODE_STRING_NT ObjectName; PVOID SecurityDescriptor; PVOID SecurityQualityOfService; } OBJECT_ATTRIBUTES_NT;  
typedef OBJECT_ATTRIBUTES_NT *POBJECT_ATTRIBUTES_NT;  
int main(POBJECT_ATTRIBUTES_NT p) { return 0; }  
