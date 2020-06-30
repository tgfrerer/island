asm(R"ASM(

	push %rbx
	movq  $0x0000, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0008, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0010, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0018, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0020, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0028, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0030, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0038, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0040, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0048, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0050, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0058, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0060, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0068, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0070, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0078, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0080, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0088, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0090, %rbx
	jmp sled_end

	push %rbx
	movq  $0x0098, %rbx
	jmp sled_end

	push %rbx
	movq  $0x00a0, %rbx
	jmp sled_end

	push %rbx
	movq  $0x00a8, %rbx
	jmp sled_end

	push %rbx
	movq  $0x00b0, %rbx
	jmp sled_end

	push %rbx
	movq  $0x00b8, %rbx
	jmp sled_end

	push %rbx
	movq  $0x00c0, %rbx
	jmp sled_end

	push %rbx
	movq  $0x00c8, %rbx
	jmp sled_end

	push %rbx
	movq  $0x00d0, %rbx
	jmp sled_end

	push %rbx
	movq  $0x00d8, %rbx
	jmp sled_end

	push %rbx
	movq  $0x00e0, %rbx
	jmp sled_end

	push %rbx
	movq  $0x00e8, %rbx
	jmp sled_end

	push %rbx
	movq  $0x00f0, %rbx
	jmp sled_end

	push %rbx
	movq  $0x00f8, %rbx
	jmp sled_end

sled_end:
)ASM");
