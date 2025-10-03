# Set a global variable
function(set_global_var name value)
  if (value)
    set_property(GLOBAL PROPERTY "${name}" "${value}")
  endif()
endfunction()

# Get a global variable
function(get_global_var name out_var)
    get_property(current GLOBAL PROPERTY "${name}" SET)
    if (current)
        get_property(current GLOBAL PROPERTY "${name}")
    endif()
    set(${out_var} "${current}" PARENT_SCOPE)
endfunction()

# Append a value to a global list variable
function(append_to_global_var name value)
    get_global_var(${name} current)
    if(current)
        list(APPEND current ${value})
    else()
        set (current "${value}")
    endif()
    set_property(GLOBAL PROPERTY "${name}" "${current}")
endfunction()

# remove an item from global variable list
function(remove_from_global_var name value)
    get_global_var(${name} current)
    list(REMOVE_ITEM current ${value})
    set_property(GLOBAL PROPERTY "${name}" "${current}")
endfunction()

# remove an item from global variable list
function(remove_duplicates_from_global_var name)
    get_global_var(${name} current)
    list(REMOVE_DUPLICATES current )
    set_property(GLOBAL PROPERTY "${name}" "${current}")
endfunction()