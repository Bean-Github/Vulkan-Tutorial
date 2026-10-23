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

	uint32_t queueIndex = ~0;        // index of a queue family found in the physical device that supports both graphics and presentation                       

	vk::raii::Queue graphicsQueue =
	    nullptr;        // this is created with the logical device and is used to submit commands to the GPU

	vk::raii::SwapchainKHR           swapChain = nullptr;
	std::vector<vk::Image>           swapChainImages;
	vk::SurfaceFormatKHR             swapChainSurfaceFormat;
	vk::Extent2D                     swapChainExtent;
	std::vector<vk::raii::ImageView> swapChainImageViews;

	vk::raii::PipelineLayout pipelineLayout   = nullptr;
	vk::raii::Pipeline       graphicsPipeline = nullptr;

	vk::raii::CommandPool commandPool = nullptr;
	vk::raii::CommandBuffer commandBuffer = nullptr;

	vk::raii::Semaphore presentCompleteSemaphore = nullptr;
	vk::raii::Semaphore renderFinishedSemaphore  = nullptr;
	vk::raii::Fence     drawFence                = nullptr;

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
		createCommandPool();
		createCommandBuffer();
		createSyncObjects();
	}

	void mainLoop()
	{
		while (!glfwWindowShouldClose(window))
		{
			glfwPollEvents();
			drawFrame();
		}

		device.waitIdle();
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

	void createCommandPool()
	{
		vk::CommandPoolCreateInfo poolInfo{
		    .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, .queueFamilyIndex = queueIndex};
		commandPool = vk::raii::CommandPool(device, poolInfo);
	}

	void createCommandBuffer()
	{
		vk::CommandBufferAllocateInfo allocInfo{
		    .commandPool = commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1};

		commandBuffer = std::move(vk::raii::CommandBuffers(device, allocInfo).front());

	}

	void recordCommandBuffer(uint32_t imageIndex)
	{
		commandBuffer.begin({});

		// Before starting rendering, transition the swapchain image to vk::ImageLayout::eColorAttachmentOptimal
		transition_image_layout(
		    imageIndex,
		    vk::ImageLayout::eUndefined,
		    vk::ImageLayout::eColorAttachmentOptimal,
		    {},        // srcAccessMask (no need to wait for previous operations)
		    vk::AccessFlagBits2::eColorAttachmentWrite,                // dstAccessMask
		    vk::PipelineStageFlagBits2::eColorAttachmentOutput,        // srcStage
		    vk::PipelineStageFlagBits2::eColorAttachmentOutput         // dstStage
		);

		// set clear color to black with full opacity, and define the rendering attachment info for the swapchain image
		// view (which image to render too and with what attachment / what to do with it, in this cae we clear it to black before and store it after), 
		// and the rendering info for the render area, layer count, and color attachment count
		vk::ClearValue              clearColor     = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
		vk::RenderingAttachmentInfo attachmentInfo = {
		    .imageView   = swapChainImageViews[imageIndex],
		    .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
		    .loadOp      = vk::AttachmentLoadOp::eClear,
		    .storeOp     = vk::AttachmentStoreOp::eStore,
		    .clearValue  = clearColor};

		vk::RenderingInfo renderingInfo = {
		    .renderArea           = {.offset = {0, 0}, .extent = swapChainExtent},
		    .layerCount           = 1,
		    .colorAttachmentCount = 1,
		    .pColorAttachments    = &attachmentInfo
		};

		commandBuffer.beginRendering(renderingInfo);

		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
		// here we define the viewport and scissor rectange, which we earlier defined as dynamic states in the graphics
		// pipeline. This allows us to change the viewport and scissor rectangle at draw time, rather than having to
		// create a new graphics pipeline for each different viewport and scissor rectangle.
		commandBuffer.setViewport(
		    0,
		    vk::Viewport(
		        0.0f,
		        0.0f,
		        static_cast<float>(swapChainExtent.width),
		        static_cast<float>(swapChainExtent.height),
		        0.0f,
		        1.0f));
		commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));

		commandBuffer.draw(3, 1, 0, 0);        // vertexCount, instanceCount, firstVertex, firstInstance

		commandBuffer.endRendering();

		// transition the swapchain image to vk::ImageLayout::ePresentSrcKHR so that it can be presented to the screen
		transition_image_layout(
			imageIndex,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::ePresentSrcKHR,
			vk::AccessFlagBits2::eColorAttachmentWrite,        // srcAccessMask
			{},                                                // dstAccessMask (no need to wait for subsequent operations)
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,        // srcStage
			vk::PipelineStageFlagBits2::eBottomOfPipe                // dstStage
		);

		commandBuffer.end();
	}

	// images can be in different layouts that are optimized for different operations. 
	// For example, an image can be in a layout that is optimal for presenting to the screen, 
	// or in a layout that is optimal for being used as a color attachment
	void transition_image_layout(
	    uint32_t                imageIndex,
	    vk::ImageLayout         old_layout,
	    vk::ImageLayout         new_layout,
	    vk::AccessFlags2        src_access_mask,
	    vk::AccessFlags2        dst_access_mask,
	    vk::PipelineStageFlags2 src_stage_mask,
	    vk::PipelineStageFlags2 dst_stage_mask)
	{
		vk::ImageMemoryBarrier2 barrier = {
		    .srcStageMask        = src_stage_mask,
		    .srcAccessMask       = src_access_mask,
		    .dstStageMask        = dst_stage_mask,
		    .dstAccessMask       = dst_access_mask,
		    .oldLayout           = old_layout,
		    .newLayout           = new_layout,
		    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		    .image               = swapChainImages[imageIndex],
		    .subresourceRange    = {
		           .aspectMask     = vk::ImageAspectFlagBits::eColor,
		           .baseMipLevel   = 0,
		           .levelCount     = 1,
		           .baseArrayLayer = 0,
		           .layerCount     = 1}};
		vk::DependencyInfo dependency_info = {
		    .dependencyFlags = {}, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
		commandBuffer.pipelineBarrier2(dependency_info);
	}


	// note: vulkan does not automatically figure out when GPU operations must wait for one another
	// you must explicitly define the dependencies between operations (ex: B must wait for A), which is done using semaphores and fences.
	// here, we must order 
	// 1) Acquire an image from the swap chain
	// 2) Execute commands that draw onto the acquired image
	// 3) Present that image to the screen for presentation, returning it to the swapchain

	// SEMAPHORE: used to add order between queue operations, used to synchronize operations between GPU-GPU
	// ex: (binary semaphore) When operation A finishes, semaphore S will be signaled, while operation B wont start until S is signaled. 
	// After operation B begins executing, semaphore S is automatically reset back to being unsignaled, allowing it to be used again. 
	// note: semaphores are not used to synchronize operations between the CPU
	// and GPU, only between GPU operations. it doesn't block host execution, and the CPU can continue executing without
	// waiting for the semaphore to be signaled.

	// FENCE: a fence coordinates GPU work with the CPU.
	// A concrete example is taking a screenshot. Say we have already done the necessary work on the GPU. 
	// Now need to transfer the image from the GPU over to the host and then save the memory to a file. 
	// We have command buffer A which executes the transfer and fence F. We submit command buffer A with fence F, then immediately tell 
	// the host to wait for F to signal. This causes the host to block until command buffer A finishes execution. 
	// Thus, we are safe to let the host save the file to disk, as the memory transfer has completed.
	
	// use semaphores to drive the swapchain image acquisition and presentation (acquire an image, then draw to it, then present it)
	// and use fences for the CPU to wait for frames to finish before starting a new one.

	void createSyncObjects()
	{
		presentCompleteSemaphore = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo());
		renderFinishedSemaphore  = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo());
		drawFence                = vk::raii::Fence(device, {.flags = vk::FenceCreateFlagBits::eSignaled});
	}

	void drawFrame()
	{
		// wait for the previous frame to finish before starting a new one. this is done by waiting for the drawFence to
		// be signaled, which indicates that the previous frame has finished executing on the GPU.
		auto fenceResult = device.waitForFences(*drawFence, vk::True, UINT64_MAX);
		if (fenceResult != vk::Result::eSuccess)
		{
			throw std::runtime_error("failed to wait for fence!");
		}
		// This resets drawFence to unsignaled so the next submission can signal it again
		device.resetFences(*drawFence);

		// acquire an image from the swap chain, and get the index of the image that was acquired. use the
		// presentCompleteSemaphore to ensure we only get swapchain image that our application can render into
		// CPU will wait indefinitely (UINT64_MAX) for an image to become available, and we signal the
		// presentCompleteSemaphore when the image is ready
		auto [result, imageIndex] = swapChain.acquireNextImage(UINT64_MAX, *presentCompleteSemaphore, nullptr);

		recordCommandBuffer(imageIndex);        // record the command buffer to draw the triangle onto the acquired swapchain image

		// submit work to the GPU. GPU will know it cannot reach color-output operations
		// until presentCompleteSemaphore is signaled
		// it submits the command buffer, and will signal the renderFinishedSemaphore when the command buffer has
		// finished executing, which will be used to signal that the image is ready for presentation
		// (so wait until presentCompleteSemaphore before executing the command buffer, and signal
		// renderFinishedSemaphore when the command buffer has finished executing)
		vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
		const vk::SubmitInfo   submitInfo{
		      .waitSemaphoreCount   = 1,
		      .pWaitSemaphores      = &*presentCompleteSemaphore,
		      .pWaitDstStageMask    = &waitDestinationStageMask,
		      .commandBufferCount   = 1,
		      .pCommandBuffers      = &*commandBuffer,
		      .signalSemaphoreCount = 1,
		      .pSignalSemaphores    = &*renderFinishedSemaphore};        // signal the renderFinishedSemaphore when the
		                                                              // command buffer has finished executing

		// submit the command buffer to the graphics queue, and when that is done, signal drawFence to indicate that the
		// frame has finished executing on the GPU
		graphicsQueue.submit(submitInfo, *drawFence);

		// tell GPU to present the image to the screen, waiting on renderFinishedSemaphore before presenting the image.
		const vk::PresentInfoKHR presentInfoKHR{
		    .waitSemaphoreCount = 1,
		    .pWaitSemaphores    = &*renderFinishedSemaphore,
		    .swapchainCount     = 1,
		    .pSwapchains        = &*swapChain,
		    .pImageIndices      = &imageIndex};

		result = graphicsQueue.presentKHR(presentInfoKHR);
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