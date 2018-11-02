


namespace le {

// Codegen <VkAttachmentStoreOp>
enum class AttachmentStoreOp : uint32_t {
        eStore    = 0, // << most common case
        eDontCare = 1,
};
// Codegen </VkAttachmentStoreOp>

// leave me alone. 
struct b {};



// Codegen <VkSampleCountFlagBits>
enum class ImageTiling : uint32_t {
        eOptimal = 0,
        eLinear  = 1,
};
// Codegen </VkSampleCountFlagBits>

struct a {};

}