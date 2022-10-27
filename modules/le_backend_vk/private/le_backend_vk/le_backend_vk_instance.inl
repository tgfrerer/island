#pragma once

// we use this file to make le_backend_vk_instance_o public to le_backend_vk.cpp and to le_instance_vk.cpp


struct le_backend_vk_instance_o {
	VkInstance               vkInstance     = nullptr;
	VkDebugUtilsMessengerEXT debugMessenger = nullptr;

	std::set<std::string> instanceExtensionSet{};
	bool                  is_using_validation_layers = false;
};
