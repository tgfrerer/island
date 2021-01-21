# Hint 1: Build Island Out-of-Tree

Island fully supports out-of-tree builds. While the term "out-of-tree" is a bit ambiguous, for this discussion it means that you can place your app directory alongside the main Island repository and keep track of Island and your app code in separate directories.

This works by updating the `CMakeLists.txt` variable [`ISLAND_BASE_DIR`](https://github.com/tgfrerer/island/blob/47b8d92e1f90d6df999d38751bd915b585238fa5/apps/examples/hello_triangle/CMakeLists.txt#L20) to the relative path pointing from your app base directory to the Island base directory.

# Hint 2: Add Island Repository as a Submodule to your App Repository

Sometimes it can be a good idea to add Island as a submodule to your application repository. For this to work, you will have to update your app's `ISLAND_BASE_DIR` setting (see Hint 1).

Say, you set up a directory structure as follows:

````txt
+ my_island_project
\
 |- island @47b892 (git submodule via: https://github.com/tgfrerer/island.git)
 |
 |- extra_modules @be2823 (git submodule via https://github.com/some_user/island_modules.git)
 | \
 |  |- le_frobnicate
 |  |
 |  |- le_bafurcate
 |
 |- my_apps
 | \ 
 |  |- my_test_app_1
 |  |
 |  |- my_test_app_2

````

## What are the benefits? 

You get *Known-Good* versions for all your Island app dependencies.

If you create a `my_island_project` git repository, and then add to it [the main island repository](https://github.com/tgfrerer/island.git) as a git submodule, this allows you to store a *Known Good* version of Island with your app. If someone else then clones your project and (checks out all submodules recursively) they will get exaclty the right versions of Island and any other dependencies.


# Hint 2: Extra Custom Modules!

Sometimes the [modules coming pre-packaged](./modules) with Island are not enough - either you want to use your own modules library or you might want to use modules, made available from other folks. 

This is possible via the `add_island_module_location()` macro which you can call from your app's `CMakeLists.txt` file. [Example](https://github.com/tgfrerer/island/blob/47b8d92e1f90d6df999d38751bd915b585238fa5/apps/examples/hello_triangle/CMakeLists.txt#L30). You can add one or more paths so that these will be scanned additionally when Island searches for modules. 

Then, call `add_island_module(my_extra_module_name)` to add your custom module just as you would add any Island core module. If you're using Qt Creator, you should see the module added to your source file view immediately.

Similarly to checking in the main Island repository as a submodule to your app repository (See: Hint 1), adding extra module source directories as submodules can help you keep track of extra modules used for a particular project.
