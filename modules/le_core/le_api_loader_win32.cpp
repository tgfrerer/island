#include "le_api_loader.h"

#ifdef LE_API_LOADER_IMPL_WIN32

#	pragma comment( lib, "Rstrtmgr.lib" )
#	pragma comment( lib, "ntdll.lib" )
#	include <windows.h>
#	include <RestartManager.h>
#	include <stdio.h>
#	include <winternl.h>
#	include <vector>
#	include <string>
#	include <cassert>
#	include <memory>
#	include "assert.h"
#	include <string>
#	include <iostream>
#	include <filesystem>
#	include "le_log/le_log.h"

struct le_file_watcher_o;

#	define LOG_PREFIX_STR "loader"

// declare function pointer type to register_fun function
typedef void ( *register_api_fun_p_t )( void * );

struct le_module_loader_o {
	std::string        mApiName;
	std::string        mRegisterApiFuncName;
	std::string        mPath;
	void *             mLibraryHandle = nullptr;
	le_file_watcher_o *mFileWatcher   = nullptr;
};

bool grab_and_drop_pdb_handle( char const *path ); // ffdecl
bool delete_old_artifacts( char const *path );     //ffdecl

// ----------------------------------------------------------------------

static le_log_channel_o *get_logger( le_log_channel_o **logger ) {
	// First, initialise logger to nullptr so that we can test against this when the logger module gets loaded.
	*logger = nullptr;
	// Next call will initialise logger by calling into this library.
	*logger = le_log::api->get_channel( LOG_PREFIX_STR );
	return *logger;
};

static le_log_channel_o *logger = get_logger( &logger );

// ----------------------------------------------------------------------

static void log_printf( FILE *f_out, const char *msg, ... ) {
	fprintf( f_out, "[ %-20.20s ] ", LOG_PREFIX_STR );
	va_list arglist;
	va_start( arglist, msg );
	vfprintf( f_out, msg, arglist );
	va_end( arglist );
	fprintf( f_out, "\n" );
	if ( f_out == stderr ) {
		fflush( f_out );
	}
}

template <typename... Args>
static void log_info( const char *msg, Args &&... args ) {
	if ( logger ) {
		le_log::le_log_channel_i.info( logger, msg, std::move( args )... );
	} else {
		log_printf( stdout, msg, args... );
	}
}

// ----------------------------------------------------------------------
template <typename... Args>
static void log_error( const char *msg, Args &&... args ) {
	if ( logger ) {
		le_log::le_log_channel_i.error( logger, msg, std::move( args )... );
	} else {
		log_printf( stderr, msg, args... );
	}
}

// ----------------------------------------------------------------------

static void unload_library( void *handle_, const char *path ) {
	if ( handle_ ) {
		auto result = FreeLibrary( static_cast<HMODULE>( handle_ ) );
		// we must detect whether the module that was unloaded was the logger module -
		// in which case we can't log using the logger module.

		// log_info( " %10s %-20s: %-50s, handle: %p ", "", "Close Module", path, handle_ );

		if ( 0 == result ) {
			auto error = GetLastError();
			log_error( "%10s %-20s: handle: %p, error: %s", "ERROR", "dlclose", handle_, error );
		} else {
			if ( grab_and_drop_pdb_handle( path ) ) {
				delete_old_artifacts( path );
			} else {
				log_error( "%10s %-20s: %s\n", "ERROR", "DropHandles", "Could not drop pdb handles." );
			}
		}
	}
}

// ----------------------------------------------------------------------

static void *load_library( const char *lib_name ) {

	void *handle = LoadLibrary( lib_name );
	if ( handle == NULL ) {
		auto loadResult = GetLastError();
		log_error( "FATAL ERROR: %d", loadResult );
		exit( 1 );
	} else {
		log_info( "[%-10s] %-20s: %-50s, handle: %p", "OK", "Loaded Module", lib_name, handle );
	}
	return handle;
}

// ----------------------------------------------------------------------

static bool load_library_persistent( const char *lib_name ) {
	return false;
}

// ----------------------------------------------------------------------

static le_module_loader_o *instance_create( const char *path_ ) {
	le_module_loader_o *tmp = new le_module_loader_o{};
	tmp->mPath              = path_;
	return tmp;
};

// ----------------------------------------------------------------------

static void instance_destroy( le_module_loader_o *obj ) {
	unload_library( obj->mLibraryHandle, obj->mPath.c_str() );
	delete obj;
};

// ----------------------------------------------------------------------

static bool load( le_module_loader_o *obj ) {
	unload_library( obj->mLibraryHandle, obj->mPath.c_str() );
	obj->mLibraryHandle = load_library( obj->mPath.c_str() );
	return ( obj->mLibraryHandle != nullptr );
}

// ----------------------------------------------------------------------

static bool register_api( le_module_loader_o *obj, void *api_interface, const char *register_api_fun_name ) {
	// define function pointer we will use to initialise api
	register_api_fun_p_t fptr;
	// load function pointer to initialisation method

	FARPROC fp;

	fp = GetProcAddress( ( HINSTANCE )obj->mLibraryHandle, register_api_fun_name );
	if ( !fp ) {
		log_error( "ERROR: '%d'", GetLastError() );
		assert( false );
		return false;
	}

	// Initialize the API. This means telling the API to populate function
	// pointers inside the struct which we are passing as parameter.
	log_info( "Register Module: '%s'", register_api_fun_name );

	fptr = ( register_api_fun_p_t )fp;
	( *fptr )( api_interface );
	return true;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_module_loader, p_api ) {
	auto  api                          = static_cast<le_module_loader_api *>( p_api );
	auto &loader_i                     = api->le_module_loader_i;
	loader_i.create                    = instance_create;
	loader_i.destroy                   = instance_destroy;
	loader_i.load                      = load;
	loader_i.register_api              = register_api;
	loader_i.load_library_persistently = load_library_persistent;
}

// Windows - specific: we must provide with a method to wrest the stale pdb file
// from the visual studio debugger's hands, if it is running.

constexpr ULONG STATUS_INFO_LENGTH_MISMATCH   = 0xc0000004;
constexpr ULONG STATUS_INSUFFICIENT_RESOURCES = 0xC000009A;
constexpr ULONG STATUS_BUFFER_OVERFLOW        = 0x80000005;
#    define SystemHandleInformation 16

#    define ObjectNameInformation 1
#    define ObjectAllTypesInformation 3

typedef NTSTATUS( NTAPI *_NtQuerySystemInformation )(
    ULONG  SystemInformationClass,
    PVOID  SystemInformation,
    ULONG  SystemInformationLength,
    PULONG ReturnLength );
typedef NTSTATUS( NTAPI *_NtDuplicateObject )(
    HANDLE      SourceProcessHandle,
    HANDLE      SourceHandle,
    HANDLE      TargetProcessHandle,
    PHANDLE     TargetHandle,
    ACCESS_MASK DesiredAccess,
    ULONG       Attributes,
    ULONG       Options );
typedef NTSTATUS( NTAPI *_NtQueryObject )(
    HANDLE ObjectHandle,
    ULONG  ObjectInformationClass,
    PVOID  ObjectInformation,
    ULONG  ObjectInformationLength,
    PULONG ReturnLength );

typedef struct _SYSTEM_HANDLE {
	ULONG       ProcessId;
	BYTE        ObjectTypeNumber;
	BYTE        Flags;
	USHORT      Handle;
	PVOID       Object;
	ACCESS_MASK GrantedAccess;
} SYSTEM_HANDLE, *PSYSTEM_HANDLE;

typedef struct _SYSTEM_HANDLE_INFORMATION {
	ULONG         HandleCount;
	SYSTEM_HANDLE Handles[ 1 ];
} SYSTEM_HANDLE_INFORMATION, *PSYSTEM_HANDLE_INFORMATION;

//
typedef struct _OBJECT_TYPE_INFORMATION {
	UNICODE_STRING  TypeName;
	ULONG           TotalNumberOfObjects;
	ULONG           TotalNumberOfHandles;
	ULONG           TotalPagedPoolUsage;
	ULONG           TotalNonPagedPoolUsage;
	ULONG           TotalNamePoolUsage;
	ULONG           TotalHandleTableUsage;
	ULONG           HighWaterNumberOfObjects;
	ULONG           HighWaterNumberOfHandles;
	ULONG           HighWaterPagedPoolUsage;
	ULONG           HighWaterNonPagedPoolUsage;
	ULONG           HighWaterNamePoolUsage;
	ULONG           HighWaterHandleTableUsage;
	ULONG           InvalidAttributes;
	GENERIC_MAPPING GenericMapping;
	ULONG           ValidAccessMask;
	BOOLEAN         SecurityRequired;
	BOOLEAN         MaintainHandleCount;
	UCHAR           TypeIndex; // since WINBLUE
	CHAR            ReservedByte;
	ULONG           PoolType;
	ULONG           DefaultPagedPoolCharge;
	ULONG           DefaultNonPagedPoolCharge;
} OBJECT_TYPE_INFORMATION, *POBJECT_TYPE_INFORMATION;

// sanity check
static_assert( sizeof( PUBLIC_OBJECT_TYPE_INFORMATION ) == sizeof( OBJECT_TYPE_INFORMATION ), "size must match" );

typedef struct _OBJECT_ALL_INFORMATION {
	ULONG                   NumberOfObjectsTypes;
	OBJECT_TYPE_INFORMATION ObjectTypeInformation[ 1 ];
} OBJECT_ALL_INFORMATION, *POBJECT_ALL_INFORMATION;

PVOID GetLibraryProcAddress( LPCSTR LibraryName, LPCSTR ProcName ) {
	HMODULE module = GetModuleHandleA( LibraryName );
	return module ? GetProcAddress( module, ProcName ) : NULL;
}

// We use some RAII to keep sane with all the handles flying around.
// Handles are wrapped in a custom unique_ptr<>, for which this is
// the deleter Functor.
struct HandleDeleter {
	void operator()( HANDLE *h ) {
		if ( h ) {
			CloseHandle( *h );
		}
		delete ( h );
	};
};

// ----------------------------------------------------------------------

UCHAR get_file_handle_object_type_index( HANDLE processHandle ) {

	// Find TypeIndex for handles which are labelled "File",
	// so that we may filter file handles more easily
	// by TypeIndex.

	constexpr auto FILE_LABEL = L"File";

	ULONG             resultLength = 1024;
	std::vector<BYTE> objInformation;

	LONG query_result = 0;

	while ( objInformation.size() < resultLength ) {

		objInformation.resize( resultLength );

		query_result = NtQueryObject(
		    processHandle,
		    ( OBJECT_INFORMATION_CLASS )ObjectAllTypesInformation,
		    objInformation.data(),
		    ULONG( objInformation.size() ),
		    &resultLength );
	}

	BYTE * data         = objInformation.data();
	BYTE * data_end     = data + objInformation.size();
	size_t num_elements = ( ( POBJECT_ALL_INFORMATION )( data ) )->NumberOfObjectsTypes;
	data                = ( BYTE * )( ( POBJECT_ALL_INFORMATION )( data ) )->ObjectTypeInformation;

	POBJECT_TYPE_INFORMATION info;
	for ( size_t i = 0; data != data_end && i != num_elements; i++ ) {

		info = ( POBJECT_TYPE_INFORMATION )( data );

		if ( 0 == wcsncmp( FILE_LABEL, info->TypeName.Buffer, info->TypeName.Length / 2 ) ) {
			return info->TypeIndex;
			break;
		}

		// What a mess, but thankfully there was some information to be found about how these
		// Object Information Records are presented to the caller:
		// https://www.geoffchappell.com/studies/windows/km/ntoskrnl/api/ob/obquery/type.htm
		//
		// We must round up to the next multiple of 8, which is what ( ( x + 7 ) >> 3 ) << 3 ) does.
		size_t offset = ( ( sizeof( OBJECT_TYPE_INFORMATION ) + info->TypeName.MaximumLength + 7 ) >> 3 ) << 3;
		data += offset;
	}

	return 0;
}

// ----------------------------------------------------------------------

bool close_handles_held_by_process_id( ULONG process_id, wchar_t const *needle_suffix ) {

	// Grab function pointers to methods which we will need from ntdll.

	static _NtQuerySystemInformation NtQuerySystemInformation = ( _NtQuerySystemInformation )GetLibraryProcAddress( "ntdll.dll", "NtQuerySystemInformation" );
	static _NtDuplicateObject        NtDuplicateObject        = ( _NtDuplicateObject )GetLibraryProcAddress( "ntdll.dll", "NtDuplicateObject" );
	static _NtQueryObject            NtQueryObject            = ( _NtQueryObject )GetLibraryProcAddress( "ntdll.dll", "NtQueryObject" );

	// Duplicate process based on process_id, so that we can query the process.
	std::unique_ptr<HANDLE, HandleDeleter> pHandle( new HANDLE( OpenProcess( PROCESS_DUP_HANDLE, FALSE, process_id ) ) );

	if ( !pHandle ) {
		printf( "Could not open PID %d! (Don't try to open a system process.)\n", process_id );
		return false;
	}

	// Find out all file handles held by the process.

	// Handle type index given for handle of type file in this process.
	// We assume that this value is the same over all processes,
	// and doesn't change for the lifetime of this program.
	UCHAR const FILE_HANDLE_OBJECT_TYPE_INDEX = get_file_handle_object_type_index( *pHandle.get() );

	// Find Object Type Index which is given to Objects of type File,
	// by finding the first object labelled File, and taking note of its
	// Type Index.

	std::vector<BYTE> buf;
	ULONG             ReturnLength = 1024;
	NTSTATUS          status       = STATUS_INFO_LENGTH_MISMATCH;

	while ( buf.size() < ReturnLength || status == STATUS_INFO_LENGTH_MISMATCH ) {
		buf.resize( ReturnLength );
		status = NtQuerySystemInformation( SystemHandleInformation, buf.data(), ReturnLength, &ReturnLength );
		if ( status == STATUS_INSUFFICIENT_RESOURCES ) {
			exit( 1 );
		}
	}

	PSYSTEM_HANDLE_INFORMATION pshti = ( PSYSTEM_HANDLE_INFORMATION )( buf.data() );

	ULONG_PTR NumberOfHandles = pshti->HandleCount;

	if ( NumberOfHandles == 0 ) {
		return L"";
	}

	_SYSTEM_HANDLE *sys_handle = pshti->Handles;
	auto const      handle_end = sys_handle + NumberOfHandles;

	for ( ; sys_handle != handle_end; sys_handle++ ) {

		// Filter for handles which are held by the given processId
		if ( sys_handle->ProcessId != process_id ) {
			continue;
		}

		// Query the object name (unless it has an access of
		//   0x0012019f, on which NtQueryObject could hang.
		if ( sys_handle->GrantedAccess == 0x0012019f ) {
			continue;
		}

		if ( sys_handle->GrantedAccess == 0x00120189 ) {
			continue;
		}

		// Filter for handles of type file
		if ( sys_handle->ObjectTypeNumber != FILE_HANDLE_OBJECT_TYPE_INDEX ) {
			continue;
		}

		std::unique_ptr<HANDLE, HandleDeleter> pFileHandle( new HANDLE );

		// Duplicate the handle so we can query it.
		if ( !NT_SUCCESS( NtDuplicateObject(
		         *pHandle,
		         ( void * )sys_handle->Handle,
		         GetCurrentProcess(),
		         pFileHandle.get(),
		         0,
		         0,
		         0 ) ) ) {
			printf( "Warning: NtDuplicateObject: could not duplicate process handle [%#x]\n", sys_handle->Handle );
			fflush( stdout );
			return false;
		}

		// Query the object name.
		std::vector<BYTE> objectName;
		{
			ULONG objectNameSize = 16;

			while ( objectName.size() < objectNameSize ) {
				objectName.resize( objectNameSize );

				auto status =
				    NtQueryObject(
				        *pFileHandle,
				        ObjectNameInformation,
				        objectName.data(),
				        ULONG( objectName.size() ),
				        &objectNameSize );

				if ( status != STATUS_BUFFER_OVERFLOW ) {
					break;
				}
			}
		}
		PUNICODE_STRING name_info = PUNICODE_STRING( objectName.data() );

		const auto needle_len       = wcslen( needle_suffix );
		const auto needle_num_bytes = needle_len * sizeof( wchar_t );

		if ( name_info->Length < needle_num_bytes ) {
			continue;
		}

		auto hay_tail = name_info->Buffer + ( name_info->Length / 2 ) - needle_len;

		if ( 0 == wcsncmp( needle_suffix, hay_tail, needle_len ) ) {

			pFileHandle.reset( new HANDLE );

			if ( !NT_SUCCESS( NtDuplicateObject(
			         *pHandle,
			         ( void * )sys_handle->Handle,
			         GetCurrentProcess(),
			         pFileHandle.get(),
			         0,
			         FALSE,
			         DUPLICATE_CLOSE_SOURCE // This means to close the source handle when duplicating, effectively taking ownership of the handle.
			         ) ) ) {

				printf( "Error: Could not duplicate Handle [%#x] \n", sys_handle->Handle );
				fflush( stdout );
				continue;
			}

			// Forcibly close the handle - drop it.
			//
			// Note that this does not yet delete the handle - it just means that we took ownership
			// away from the processes which held the handle.
			pFileHandle.reset();

			// printf( "Dropped handle: [0x%04x]: %.*S\r\n", sys_handle->Handle, name_info->Length / 2, name_info->Buffer );
			// fflush( stdout );
			return true;
		}
	};

	return false;
}

// ----------------------------------------------------------------------

std::vector<DWORD> enumerate_processes_holding_handle_to_file( PCWSTR file_path, wchar_t const *suffix ) {

	std::vector<DWORD> result;

	DWORD dwSession;
	WCHAR szSessionKey[ CCH_RM_SESSION_KEY + 1 ] = { 0 };
	DWORD dwError                                = RmStartSession( &dwSession, 0, szSessionKey );
	//wprintf(L"RmStartSession returned %d\n", dwError);
	if ( dwError != ERROR_SUCCESS ) {
		return {};
	}

	dwError = RmRegisterResources( dwSession, 1, &file_path, 0, NULL, 0, NULL );
	//wprintf(L"RmRegisterResources(%ls) returned %d\n", pszFile, dwError);
	if ( dwError == ERROR_SUCCESS ) {
		DWORD dwReason;

		UINT            nProcInfoNeeded;
		UINT            nProcInfo = 10;
		RM_PROCESS_INFO rgpi[ 10 ];

		dwError = RmGetList( dwSession, &nProcInfoNeeded, &nProcInfo, rgpi, &dwReason );
		//wprintf(L"RmGetList returned %d\n", dwError);

		if ( dwError == ERROR_SUCCESS ) {

			//wprintf( L"RmGetList returned %d infos (%d needed)\n", nProcInfo, nProcInfoNeeded );

			for ( UINT i = 0; i < nProcInfo; i++ ) {

				//wprintf( L"%d.ApplicationType = %d\n", i, rgpi[ i ].ApplicationType );
				//wprintf( L"%d.strAppName = %ls\n", i, rgpi[ i ].strAppName );
				//wprintf( L"%d.Process.dwProcessId = %d\n", i, rgpi[ i ].Process.dwProcessId );

				HANDLE hProcess = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, rgpi[ i ].Process.dwProcessId );

				if ( hProcess ) {

					FILETIME ftCreate, ftExit, ftKernel, ftUser;
					if ( GetProcessTimes( hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser ) &&
					     CompareFileTime( &rgpi[ i ].Process.ProcessStartTime, &ftCreate ) == 0 ) {
						WCHAR sz[ MAX_PATH ];
						DWORD cch = MAX_PATH;
						if ( QueryFullProcessImageNameW( hProcess, 0, sz, &cch ) && cch <= MAX_PATH ) {
							//wprintf( L"  = %ls\n", sz );
							result.push_back( rgpi[ i ].Process.dwProcessId );
						}
					}
					CloseHandle( hProcess );
				}
			}
		}
	}
	RmEndSession( dwSession );

	return result;
}

// ----------------------------------------------------------------------

bool grab_and_drop_pdb_handle( char const *path ) {

	// convert path to wstring

	auto file_path = std::filesystem::canonical( path );

	if ( file_path.extension() == ".dll" ) {
		file_path.replace_extension( ".pdb.old" );
	} else {
		return false;
	}

	size_t path_len = strlen( file_path.string().c_str() );

	std::wstring path_wstr;
	path_wstr.resize( path_len );

	MultiByteToWideChar( CP_UTF8, MB_PRECOMPOSED, file_path.string().c_str(), int( path_len ), path_wstr.data(), int( path_wstr.size() ) );

	auto handle_suffix = L".pdb.old";

	auto process_ids = enumerate_processes_holding_handle_to_file( path_wstr.c_str(), handle_suffix );

	if ( process_ids.empty() ) {
		return true;
	}

	for ( auto const &process_id : process_ids ) {
		if ( false == close_handles_held_by_process_id( process_id, handle_suffix ) ) {
			// error: we could not close handle for some reason. abort mission.
			return false;
		};
	}

	return true;
}

// ----------------------------------------------------------------------

bool delete_old_artifacts( char const *path ) {

	// Attempts to delete a "${path}.dll.old", and "${path}.pdb.old" file
	// where ${path} is basename of path, e.g. path == './le_renderer'

	auto pdb_file_path = std::filesystem::canonical( path );

	if ( pdb_file_path.extension() == ".dll" ) {
		pdb_file_path.replace_extension( ".pdb.old" );
	} else {
		return false;
	}

	{
		// delete old pdb file now that all handles to it have been dropped.
		auto result = CreateFile( pdb_file_path.string().c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL );
		CloseHandle( result );
	}
	{
		// Delete old dll
		auto dll_file_path = std::filesystem::canonical( path );
		dll_file_path.replace_extension( ".dll.old" );
		auto result = CreateFile( dll_file_path.string().c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL );
		CloseHandle( result );
	}

	return true;
}

#endif