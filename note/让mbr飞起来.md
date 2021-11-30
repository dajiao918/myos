# 让mbr飞起来

​		前面已经提到，BIOS在最后会检测第0个扇区是否是mbr，如果是的话BIOS就会将计算机的接力棒传递给mbr。

​		换句话说，只要我们在第0个扇区的最后2个字节写上0x55，0xaa，那么这个扇区的内容就会加载到0x7c00，然后BIOS会执行

jmp 0:0x7c00开始执行mbr。

​		由于mbr只有512字节的大小，在加载内核之前，我们不仅要开启保护模式，还要检测内存，以及分页，所以mbr这么点空间是不太够的，因此mbr的主要任务就是加载内核加载器。



## 读取硬盘

​	需要知道的一点是，cpu从不和外设打交道，他只会通过IO接口传输自己的控制信号，然后由IO接口通知外设，这样CPU才能获取自己想要的数据。在很久以前，硬盘和硬盘控制器是分开的，后来业界里面的一些大佬将硬盘和硬盘控制器合并了起来。这种合并起来的接口被称为集成设备电路IDE(Integrated Drive Electronics)，所以后来的硬盘习惯性的被称为IDE硬盘，但是随着IDE的影响力越来越大，这个接口使用的技术就被归纳为全球标准技术，称之为ATA(Advantced technologt attachment)。后来又有人发明出来了一种串行接口，被称为SATA(Serial ATA)，所以以前的接口就被称为PATA（Parallel ATA）

​	一块主板支持4个IDE（PATA）硬盘，并且提供两个插槽，一个插槽是IDE0，一个是IDE1，IDE0又被称为Primary通道，IDE1被称为Secondary通道。由于一个IDE线可以挂两块硬盘，一个称为主盘，一个称为从盘。所以Primary通道和Secondary通道都有一个主盘和从盘。

​	

## 硬盘控制器端口

| primary通道 | secondary通道 | 读操作       | 写操作       |
| ----------- | ------------- | ------------ | ------------ |
| 寄存器端口  | 寄存器端口    |              |              |
| 0x1F0       | 0x170         | data         | data         |
| 0x1F1       | 0x171         | Error        | Features     |
| 0x1f3       | 0x172         | Sector Count | Sector Count |
| 0x1F3       | 0x173         | LBA low      | LBA low      |
| 0x1F4       | 0x174         | LBA mid      | LBA mid      |
| 0x1F5       | 0x175         | LBA high     | LBA high     |
| 0x1F6       | 0x176         | Device       | Device       |
| 0x1F7       | 0x177         | Status       | Command      |

*  data寄存器是数据寄存器，宽度是16位，可读可写。主要功能就是向硬盘读取数据或写入数据
* Error寄存器的作用是当读取数据失败时，寄存器里面会记录失败的信息
* Sector Count寄存器用来指定将要读取的扇区或者将要写入的扇区

LBA称为逻辑块地址，分为LBA28和LBA48，用28位或者是48位来描述一个扇区的地址。为了实现简单，系统使用LBA28的地址。

* LBA low指定一个扇区的0-7位数值
* LBA mid指定扇区的8-15位数值
* LBA high指定扇区的16-23位数值
* Device寄存器的低四位用来指定扇区24-27位地址，第4位指定选择主盘还是从盘，0代表主盘，1代表从盘。第6位用来设置是否启用LBA模式，1代表使用LBA模式。第5位和第7位固定为1
* Status寄存器用来识别硬盘的状态，第0位是error位，如果此位为1，代表命令出错。第3位是data的request位，如果此位为1，代表硬盘已经把数据准备好了，可以读数据了。第7位为1表示硬盘当前正忙，勿扰。
* Command寄存器用来设置硬盘的工作方式
  * 0xEC：识别硬盘的信息
  * 0x20：读扇区
  * 0x30：写扇区



所以对硬盘进行操作的步骤为：

1. 选择通，也就是选择寄存器端口，写入将要读取或者写入的扇区
2. 选择LBA扇区，设置Device，LBA low，LBA high， LBAmid
3. 设置主盘或者是从盘，以及启用LBA模式
4. 向Command寄存器写入操作命令
5. 读取Status寄存器，判断硬盘是否准备好
6. 读出数据或写入数据

伪代码：

```asm
;输入eax=读取的扇区号,cx为读取的扇区数，bx将数据写入的内存地址

;备份cx
mov di,cx
;1.写入将要读取或者写入的扇区
mov dx,0x1f2
mov al,cl
out dx,al

;2.选择LBA扇区
inc dx
out dx,al
inc dx
mov cl,8
shr eax,cl
out dx,al
inc dx
shr eax,cl
out dx,al

;3.设置主盘或者是从盘，以及启用LBA模式
inc dx
shr al,cl
and al,0x0f
or al,0xe0
out dx,al

;4向Command寄存器写入操作命令
inc dx
mov al,0x20
out dx,al

;5读取Status寄存器，判断硬盘是否准备好
.ready:
	in al,dx
	and al,0x88
	cmp al,0x08
	jnz .ready

mov ax,di ;读取的扇区数
mov dx,256
mul dx;ax中就是读取的字数
mov cx,ax

;读取数据
mov dx,0x1f0
.read:
	in ax,dx
	mov word [bx],ax
	add bx,2
	loop .read
	ret
```



所以只要我们调用这个例程，将内核加载器从硬盘中读取到内存中，再使用jmp指令跳转到内核加载器。mbr的实名就完成了。