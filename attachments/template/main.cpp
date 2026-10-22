#define NOMINMAX

#include <algorithm>
#include <assert.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#	include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

constexpr uint32_t WIDTH  = 800;
constexpr uint32_t HEIGHT = 600;

const std::vector<char const *> validationLayers = {"VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

class HelloTriangleApplication
{
  public:
	void run()
	{
		initWindow();
		initVulkan();
		mainLoop();
		cleanup();
	}

  private:
	GLFWwindow *window = nullptr;

	vk::raii::Context                context;
	vk::raii::Instance               instance       = nullptr;
	vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;

	vk::raii::SurfaceKHR surface = nullptr;

	vk::raii::PhysicalDevice physicalDevice =
	    nullptr;                              // Physical device (GPU) that will be used to run the application
	vk::raii::Device device = nullptr;        // Logical device

	vk::raii::Queue graphicsQueue =
	    nullptr;        // this is created with the logical device and is used to submit commands to the GPU

	vk::raii::SwapchainKHR           swapChain = nullptr;
	std::vector<vk::Image>           swapChainImages;
	vk::SurfaceFormatKHR             swapChainSurfaceFormat;
	vk::Extent2D                     swapChainExtent;
	std::vector<vk::raii::ImageView> swapChainImageViews;

	vk::raii::PipelineLayout pipelineLayout   = nullptr;
	vk::raii::Pipeline       graphicsPipeline = nullptr;

	std::vector<const char *> requiredDeviceExtension = {vk::KHRSwapchainExtensionName};

	void initWindow()
	{
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
	}

	void initVulkan()
	{
		createInstance();
		setupDebugMessenger();
		createSurface();
		pickPhysicalDevice();
		createLogicalDevice();
		createSwapChain();
		createImageViews();

		createGraphicsPipeline();
	}

	void mainLoop()
	{
		while (!glfwWindowShouldClose(window))
		{
			glfwPollEvents();
		}
	}

	void cleanup()
	{
		glfwDestroyWindow(window);

		glfwTerminate();
	}

	// a vulkan instance is the connection between our application and the Vulkan library. It is created with a set of
	// application information, validation layers, and extensions that we want to use. The instance is responsible for
	// managing the resources and objects that we create in our application, and it provides access to the physical
	// devices (GPUs) that are available on the system.
	void createInstance()
	{
		constexpr vk::ApplicationInfo appInfo{
		    .pApplicationName   = "Hello Triangle",
		    .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
		    .pEngineName        = "No Engine",
		    .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
		    .apiVersion         = vk::ApiVersion14};

		// Get the required layers
		std::vector<char const *> requiredLayers;
		if (enableValidationLayers)
		{
			requiredLayers.assign(validationLayers.begin(), validationLayers.end());
		}

		// Check if the required layers are supported by the Vulkan implementation.
		auto layerProperties    = context.enumerateInstanceLayerProperties();
		auto unsupportedLayerIt = std::ranges::find_if(requiredLayers, [&layerProperties](auto const &requiredLayer) {
			return std::ranges::none_of(layerProperties, [requiredLayer](auto const &layerProperty) {
				return strcmp(layerProperty.layerName, requiredLayer) == 0;
			});
		});
		if (unsupportedLayerIt != requiredLayers.end())
		{
			throw std::runtime_error("Required layer not supported: " + std::string(*unsupportedLayerIt));
		}

		// Get the required extensions.
		auto requiredExtensions = getRequiredInstanceExtensions();

		// Check if the required extensions are supported by the Vulkan implementation.
		auto extensionProperties = context.enumerateInstanceExtensionProperties();
		auto unsupportedPropertyIt =
		    std::ranges::find_if(requiredExtensions, [&extensionProperties](auto const &requiredExtension) {
			    return std::ranges::none_of(extensionProperties, [requiredExtension](auto const &extensionProperty) {
				    return strcmp(extensionProperty.extensionName, requiredExtension) == 0;
			    });
		    });
		if (unsupportedPropertyIt != requiredExtensions.end())
		{
			throw std::runtime_error("Required extension not supported: " + std::string(*unsupportedPropertyIt));
		}

		vk::InstanceCreateInfo createInfo{
		    .pApplicationInfo        = &appInfo,
		    .enabledLayerCount       = static_cast<uint32_t>(requiredLayers.size()),
		    .ppEnabledLayerNames     = requiredLayers.data(),
		    .enabledExtensionCount   = static_cast<uint32_t>(requiredExtensions.size()),
		    .ppEnabledExtensionNames = requiredExtensions.data()};
		instance = vk::raii::Instance(context, createInfo);
	}

	void setupDebugMessenger()
	{
		if (!enableValidationLayers)
			return;

		vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
		    vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
		vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
		    vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
		    vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
		vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{
		    .messageSeverity = severityFlags, .messageType = messageTypeFlags, .pfnUserCallback = &debugCallback};
		debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
	}

	void createSurface()
	{
		VkSurfaceKHR _surface;
		if (glfwCreateWindowSurface(*instance, window, nullptr, &_surface) != 0)
		{
			throw std::runtime_error("failed to create window surface!");
		}
		surface = vk::raii::SurfaceKHR(instance, _surface);
	}

	bool isDeviceSuitable(vk::raii::PhysicalDevice const &physicalDevice)
	{
		// Check if the physicalDevice supports the Vulkan 1.3 API version
		bool supportsVulkan1_3 = physicalDevice.getProperties().apiVersion >= VK_API_VERSION_1_3;

		// Check if any of the queue families support both graphics and presentation to our surface
		auto     queueFamilies = physicalDevice.getQueueFamilyProperties();
		uint32_t qfpIndex      = 0;
		bool     supportsGraphicsAndPresent =
		    std::ranges::any_of(queueFamilies, [&physicalDevice, &surface = this->surface, &qfpIndex](auto const &qfp) {
			    bool const suitable = (qfp.queueFlags & vk::QueueFlagBits::eGraphics) &&
				                      physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface);
			    qfpIndex++;
			    return suitable;
		    });

		// Check if all required physicalDevice extensions are available
		auto availableDeviceExtensions     = physicalDevice.enumerateDeviceExtensionProperties();
		bool supportsAllRequiredExtensions = std::ranges::all_of(
		    requiredDeviceExtension, [&availableDeviceExtensions](auto const &requiredDeviceExtension) {
			    return std::ranges::any_of(
			        availableDeviceExtensions, [requiredDeviceExtension](auto const &availableDeviceExtension) {
				        return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0;
			        });
		    });

		// Check if the physicalDevice supports the required features
		auto features = physicalDevice.template getFeatures2<
		    vk::PhysicalDeviceFeatures2,
		    vk::PhysicalDeviceVulkan11Features,
		    vk::PhysicalDeviceVulkan13Features,
		    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
		bool supportsRequiredFeatures =
		    features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
		    features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
		    features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

		// Return true if the physicalDevice meets all the criteria
		return supportsVulkan1_3 && supportsGraphicsAndPresent && supportsAllRequiredExtensions &&
		       supportsRequiredFeatures;
	}

	// picks the correct physical device (GPU) to use for the application based on the criteria defined in
	// isDeviceSuitable()
	void pickPhysicalDevice()
	{
		std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
		auto const                            devIter         = std::ranges::find_if(
		    physicalDevices, [&](auto const &physicalDevice) { return isDeviceSuitable(physicalDevice); });
		if (devIter == physicalDevices.end())
		{
			throw std::runtime_error("failed to find a suitable GPU!");
		}
		physicalDevice = *devIter;
	}

	// a logical device is an abstraction of the physical device (GPU) that allows us to interact with it and submit
	// commands. It is created with a specific set of features and extensions that we want to use, and it provides
	// access to the queues that we will use to submit commands to the GPU. In this function, we create a logical device
	// with a queue that supports both graphics and presentation, and we enable the required features and extensions for
	// our application.
	void createLogicalDevice()
	{
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
		uint32_t                               queueIndex            = ~0;
		for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
		{
			if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
			    physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface))
			{
				// found a queue family that supports both graphics and present
				queueIndex = qfpIndex;
				break;
			}
		}
		if (queueIndex == ~0)
		{
			throw std::runtime_error("Could not find a queue for graphics and present -> terminating");
		}

		// Create a chain of feature structures
		vk::StructureChain<
		    vk::PhysicalDeviceFeatures2,
		    vk::PhysicalDeviceVulkan11Features,
		    vk::PhysicalDeviceVulkan13Features,
		    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
		    featureChain = {
		        {},                                    // vk::PhysicalDeviceFeatures2 (empty for now)
		        {.shaderDrawParameters = true},        // Enable shader draw parameters from Vulkan 1.1
		        {.dynamicRendering = true},            // Enable dynamic rendering from Vulkan 1.3
		        {.extendedDynamicState = true}         // Enable extended dynamic state from the extension
		};

		float                     queuePriority = 0.5f;
		vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
		    .queueFamilyIndex = queueIndex, .queueCount = 1, .pQueuePriorities = &queuePriority};

		vk::DeviceCreateInfo deviceCreateInfo{
		    .pNext                   = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
		    .queueCreateInfoCount    = 1,
		    .pQueueCreateInfos       = &deviceQueueCreateInfo,
		    .enabledExtensionCount   = static_cast<uint32_t>(requiredDeviceExtension.size()),
		    .ppEnabledExtensionNames = requiredDeviceExtension.data()};

		device        = vk::raii::Device(physicalDevice, deviceCreateInfo);
		graphicsQueue = vk::raii::Queue(device, queueIndex, 0);
	}

	// a swap chain is a series of images that are presented to the screen in a round-robin fashion, allowing for smooth
	// rendering and presentation of frames. The swap chain is created with a specific surface format, present mode, and
	// extent (size) that matches the window's dimensions. The swap chain images are then used as color attachments in
	// the render pass, allowing the application to render to them before presenting them to the screen.
	void createSwapChain()
	{
		vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
		swapChainExtent                                = chooseSwapExtent(surfaceCapabilities);
		uint32_t minImageCount                         = chooseSwapMinImageCount(surfaceCapabilities);

		std::vector<vk::SurfaceFormatKHR> availableFormats = physicalDevice.getSurfaceFormatsKHR(*surface);
		swapChainSurfaceFormat                             = chooseSwapSurfaceFormat(availableFormats);

		std::vector<vk::PresentModeKHR> availablePresentModes = physicalDevice.getSurfacePresentModesKHR(*surface);
		vk::PresentModeKHR              presentMode           = chooseSwapPresentMode(availablePresentModes);

		vk::SwapchainCreateInfoKHR swapChainCreateInfo{
		    .surface          = *surface,
		    .minImageCount    = minImageCount,
		    .imageFormat      = swapChainSurfaceFormat.format,
		    .imageColorSpace  = swapChainSurfaceFormat.colorSpace,
		    .imageExtent      = swapChainExtent,
		    .imageArrayLayers = 1,
		    .imageUsage       = vk::ImageUsageFlagBits::eColorAttachment,
		    .imageSharingMode = vk::SharingMode::eExclusive,
		    .preTransform     = surfaceCapabilities.currentTransform,
		    .compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque,
		    .presentMode      = presentMode,
		    .clipped          = true};

		swapChain       = vk::raii::SwapchainKHR(device, swapChainCreateInfo);
		swapChainImages = swapChain.getImages();
	}

	uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities)
	{
		auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
		if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount))
		{
			minImageCount = surfaceCapabilities.maxImageCount;
		}
		return minImageCount;
	}

	vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats)
	{
		const auto formatIt = std::ranges::find_if(availableFormats, [](const auto &format) {
			return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
		});
		return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
	}

	vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes)
	{
		assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) {
			return presentMode == vk::PresentModeKHR::eFifo;
		}));
		return std::ranges::any_of(
		           availablePresentModes,
		           [](const vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; }) ?
		           vk::PresentModeKHR::eMailbox :
		           vk::PresentModeKHR::eFifo;
	}

	vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities)
	{
		// currentExtent is only set to the special "undefined" value described above
		// when the window manager lets us choose the extent ourselves; any other value
		// means the surface already dictates a fixed extent that we must use as-is.
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		{
			return capabilities.currentExtent;
		}
		int width, height;
		glfwGetFramebufferSize(window, &width, &height);

		return {
		    std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
		    std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};
	}

	// this function creates an image view for each image in the swap chain, which allows us to use the images as color
	// attachments in the render pass (an image view describes how to access an image and which part of the image to
	// access) here, we just create a simple image view for each image
	void createImageViews()
	{
		assert(swapChainImageViews.empty());

		vk::ImageViewCreateInfo imageViewCreateInfo{
		    .viewType         = vk::ImageViewType::e2D,
		    .format           = swapChainSurfaceFormat.format,
		    .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};

		imageViewCreateInfo.components = {
		    vk::ComponentSwizzle::eIdentity,
		    vk::ComponentSwizzle::eIdentity,
		    vk::ComponentSwizzle::eIdentity,
		    vk::ComponentSwizzle::eIdentity};

		for (auto &image : swapChainImages)
		{
			imageViewCreateInfo.image = image;
			swapChainImageViews.emplace_back(device, imageViewCreateInfo);
		}
	}

	void createGraphicsPipeline()
	{
		auto shaderCode = readFile("shaders/slang.spv");

		vk::raii::ShaderModule shaderModule = createShaderModule(shaderCode);

		vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
		    .stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule, .pName = "vertMain"};

		vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
		    .stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "fragMain"};

		vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

		// create fixed functions below, define / configure Vulkan pipeline stages that are not programmable (e.g.
		// vertex input info, input assembly, viewport, rasterizer, multisampling, color blending, etc.) we define
		// dynamic states for viewport and scissor, which can be specified at drawing time
		vk::PipelineVertexInputStateCreateInfo vertexInputInfo;

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{.topology = vk::PrimitiveTopology::eTriangleList};

		vk::PipelineViewportStateCreateInfo viewportState{
		    .viewportCount = 1, .scissorCount = 1};        // we will set the viewport and scissor dynamically, here we
			                                               // only need to define the number of viewports and scissors

		vk::PipelineRasterizationStateCreateInfo rasterizer{
		    .depthClampEnable        = vk::False,
		    .rasterizerDiscardEnable = vk::False,
		    .polygonMode             = vk::PolygonMode::eFill,
		    .cullMode                = vk::CullModeFlagBits::eBack,
		    .frontFace               = vk::FrontFace::eClockwise,
		    .depthBiasEnable         = vk::False,
		    .lineWidth               = 1.0f};

		vk::PipelineMultisampleStateCreateInfo multisampling{
		    .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False};

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{
		    .blendEnable    = vk::False,
		    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
			                  vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

		vk::PipelineColorBlendStateCreateInfo colorBlending{
		    .logicOpEnable   = vk::False,
		    .logicOp         = vk::LogicOp::eCopy,
		    .attachmentCount = 1,
		    .pAttachments    = &colorBlendAttachment};

		std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};

		vk::PipelineDynamicStateCreateInfo dynamicState{
		    .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data()};

		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
		    .setLayoutCount         = 0,
		    .pushConstantRangeCount = 0};        // specifies how vulkan should handle descriptor sets and push
			                                     // constants (basically how they receive external data like uniforms)
		// setLayoutCount = 0 means we don't use any descriptor sets, and pushConstantRangeCount = 0 means we don't use
		// any push constants
		pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

		// before, the system worked by having render passes and frame buffers
		// a render pass is a collection of attachments, subpasses, and dependencies that define how the rendering will
		// be performed. It specifies the number and types of attachments (e.g., color, depth, stencil), how they will
		// be used in each subpass, and how the subpasses depend on each other. a framebuffer is a collection of image
		// views that the render pass will render to. there is 1 frame buffer per image view in the swap chain you would
		// choose a frame buffer to render to at draw time, and the render pass would define how to use that frame
		// buffer

		// (before, you would acquire image index, choose frame buffer, begin render pass on the frame buffer,
		// draw, end render pass, present image index)

		// Dynamic rendering simplifies the rendering process by eliminating the need for render pass and framebuffer objects. 
		// Instead, we can specify the color, depth, and stencil attachments directly when we begin rendering.

		vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
		    {.stageCount          = 2,
			 .pStages             = shaderStages,
			 .pVertexInputState   = &vertexInputInfo,
			 .pInputAssemblyState = &inputAssembly,
			 .pViewportState      = &viewportState,
			 .pRasterizationState = &rasterizer,
			 .pMultisampleState   = &multisampling,
			 .pColorBlendState    = &colorBlending,
			 .pDynamicState       = &dynamicState,
			 .layout              = pipelineLayout,
			 .renderPass          = nullptr},
				{.colorAttachmentCount = 1,
				.pColorAttachmentFormats =
			     &swapChainSurfaceFormat.format}};        // creates a description of the color attachments that the
			                                              // graphics pipeline expects when using dynamic
			                                              // rendering

		graphicsPipeline =
		    vk::raii::Pipeline(device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
	}

	static std::vector<char> readFile(const std::string &filename)
	{
		std::ifstream file(filename, std::ios::ate | std::ios::binary);

		if (!file.is_open())
		{
			throw std::runtime_error("failed to open file!");
		}

		std::vector<char> buffer(file.tellg());
		file.seekg(0, std::ios::beg);
		file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		file.close();

		return buffer;
	}

	[[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char> &code) const
	{
		vk::ShaderModuleCreateInfo createInfo{
		    .codeSize = code.size() * sizeof(char), .pCode = reinterpret_cast<const uint32_t *>(code.data())};

		vk::raii::ShaderModule shaderModule{device, createInfo};

		return shaderModule;
	}

	std::vector<const char *> getRequiredInstanceExtensions()
	{
		uint32_t glfwExtensionCount = 0;
		auto     glfwExtensions     = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
		if (enableValidationLayers)
		{
			extensions.push_back(vk::EXTDebugUtilsExtensionName);
		}

		return extensions;
	}

	static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
	    vk::DebugUtilsMessageSeverityFlagBitsEXT      severity,
	    vk::DebugUtilsMessageTypeFlagsEXT             type,
	    const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
	    void *)
	{
		if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError ||
		    severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
		{
			std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage
			          << std::endl;
		}

		return vk::False;
	}
};

int main()
{
	try
	{
		HelloTriangleApplication app;
		app.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}