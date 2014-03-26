/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
#ifndef _CM_ELF_H
#define _CM_ELF_H 1

typedef t_uint16 Elf32_Half;
typedef t_uint16 Elf64_Half;

typedef t_uint32 Elf32_Word;
typedef t_sint32 Elf32_Sword;
typedef t_uint32 Elf64_Word;
typedef t_sint32 Elf64_Sword;

typedef t_uint64 Elf32_Xword;
typedef t_sint64 Elf32_Sxword;
typedef t_uint64 Elf64_Xword;
typedef t_sint64 Elf64_Sxword;

typedef t_uint32 Elf32_Addr;
typedef t_uint64 Elf64_Addr;

typedef t_uint32 Elf32_Off;
typedef t_uint64 Elf64_Off;

typedef t_uint16 Elf32_Section;
typedef t_uint16 Elf64_Section;

typedef Elf32_Half Elf32_Versym;
typedef Elf64_Half Elf64_Versym;


/*********************************************
 * Header
 *********************************************/
#define EI_NIDENT (16)                              //!< Size of e_ident[]

#define EI_MAG0         0               //!< File identification
#define ELFMAG0             0x7f

#define EI_MAG1         1               //!< File identification
#define ELFMAG1             'E'

#define EI_MAG2         2               //!< File identification
#define ELFMAG2             'L'

#define EI_MAG3         3               //!< File identification
#define ELFMAG3             'F'

#define EI_CLASS        4               //!< File class
#define ELFCLASSNONE        0               //!< Invalid class
#define ELFCLASS32          1               //!< 32-bit objects
#define ELFCLASS64          2               //!< 64-bit objects

#define EI_DATA         5               //!< Data encoding
#define ELFDATANONE         0               //!< Invalid data encoding
#define ELFDATA2LSB         1               //!< 2's complement, little endian
#define ELFDATA2MSB         2               //!< 2's complement, big endian

#define EI_VERSION      6               //!< File version

#define EI_OSABI        7               //!< OS ABI identification
#define ELFOSABI_NONE       0               //!<  No extension
#define ELFOSABI_HPUX       1               //!<  HP-UX
#define ELFOSABI_NETBSD     2               //!<  NetBSD
#define ELFOSABI_LINUX      3               //!<  Linux
#define ELFOSABI_SOLARIS    6               //!<  Sun Solaris
#define ELFOSABI_AIX        7               //!<  AIX
#define ELFOSABI_IRIX       8               //!<  IRIX
#define ELFOSABI_FREEBSD    9               //!<  FreeBSD
#define ELFOSABI_TRU64      10              //!<  Compaq TRU64 UNIX
#define ELFOSABI_MODESTO    11              //!<  Novell Modesto
#define ELFOSABI_OPENBSD    12              //!<  Open BSD
#define ELFOSABI_OPENVMS    13              //!<  Open VMS
#define ELFOSABI_NSK        14              //!<  HP Non-Stop-Kernel

#define EI_ABIVERSION   8               //!< ABI version

#define EI_PAD          9               //!< Start of padding byte


typedef struct
{
    unsigned char       e_ident[EI_NIDENT];             //!< The initial bytes mark the file as an object file and provide machine-independent data with which to decode and interpret the file's contents
    Elf32_Half          e_type;                         //!< This member identifies the object file type
    Elf32_Half          e_machine;                      //!< This member's value specifies the required architecture for an individual file
    Elf32_Word          e_version;                      //!< This member identifies the object file version
    Elf32_Addr          e_entry;                        //!< This member gives the virtual address to which the system first transfers control, thus starting the process
    Elf32_Off           e_phoff;                        //!< This member holds the program header table's file offset in bytes
    Elf32_Off           e_shoff;                        //!< This member holds the section header table's file offset in bytes
    Elf32_Word          e_flags;                        //!< This member holds processor-specific flags associated with the file
    Elf32_Half          e_ehsize;                       //!< This member holds the ELF header's size in bytes
    Elf32_Half          e_phentsize;                    //!< This member holds the size in bytes of one entry in the file's program header table; all entries are the same size
    Elf32_Half          e_phnum;                        //!< This member holds the number of entries in the program header table
    Elf32_Half          e_shentsize;                    //!< This member holds a section header's size in bytes
    Elf32_Half          e_shnum;                        //!< This member holds the number of entries in the section header table
    Elf32_Half          e_shstrndx;                     //!< This member holds the section header table index of the entry associated with the section name string table
} Elf32_Ehdr;       //!< 32bit Entry Header

typedef struct
{
    unsigned char       e_ident[EI_NIDENT];             //!< The initial bytes mark the file as an object file and provide machine-independent data with which to decode and interpret the file's contents
    Elf64_Half          e_type;                         //!< This member identifies the object file type
    Elf64_Half          e_machine;                      //!< This member's value specifies the required architecture for an individual file
    Elf64_Word          e_version;                      //!< This member identifies the object file version
    Elf64_Addr          e_entry;                        //!< This member gives the virtual address to which the system first transfers control, thus starting the process
    Elf64_Off           e_phoff;                        //!< This member holds the program header table's file offset in bytes
    Elf64_Off           e_shoff;                        //!< This member holds the section header table's file offset in bytes
    Elf64_Word          e_flags;                        //!< This member holds processor-specific flags associated with the file
    Elf64_Half          e_ehsize;                       //!< This member holds the ELF header's size in bytes
    Elf64_Half          e_phentsize;                    //!< This member holds the size in bytes of one entry in the file's program header table; all entries are the same size
    Elf64_Half          e_phnum;                        //!< This member holds the number of entries in the program header table
    Elf64_Half          e_shentsize;                    //!< This member holds a section header's size in bytes
    Elf64_Half          e_shnum;                        //!< This member holds the number of entries in the section header table
    Elf64_Half          e_shstrndx;                     //!< This member holds the section header table index of the entry associated with the section name string table
} Elf64_Ehdr;       //!< 64bit Entry Header

/*
 * e_type
 */
#define ET_NONE         0                   //!< No file type
#define ET_REL          1                   //!< Relocatable file
#define ET_EXEC         2                   //!< Executable file
#define ET_DYN          3                   //!< Shared object file
#define ET_CORE         4                   //!< Core file
#define ET_LOOS         0xfe00              //!< Operating system-specific
#define ET_HIOS         0xfeff              //!< Operating system-specific
#define ET_LOPROC       0xff00              //!< Processor-specific
#define ET_HIPROC       0xffff              //!< Processor-specific

/*
 * e_machine
 */
#define EM_NONE             0               //!< No machine
#define EM_M32              1               //!< AT&T WE 32100
#define EM_SPARC            2               //!< SUN SPARC
#define EM_386              3               //!< Intel 80386
#define EM_68K              4               //!< Motorola 68000
#define EM_88K              5               //!< Motorola 88000
#define EM_860              7               //!< Intel 80860
#define EM_MIPS             8               //!< MIPS I architecture
#define EM_S370             9               //!< IBM System/370
#define EM_MIPS_RS3_LE      10              //!< MIPS R3000 little-endian
#define EM_PARISC           15              //!< HPPA
#define EM_VPP500           17              //!< Fujitsu VPP500
#define EM_SPARC32PLUS      18              //!< Enhanced instruction set SPARC
#define EM_960              19              //!< Intel 80960
#define EM_PPC              20              //!< PowerPC
#define EM_PPC64            21              //!< 64-bit PowerPC
#define EM_S390             22              //!< IBM System/390 Processor
#define EM_V800             36              //!< NEC V800
#define EM_FR20             37              //!< Fujitsu FR20
#define EM_RH32             38              //!< TRW RH-32
#define EM_RCE              39              //!< Motorola RCE
#define EM_ARM              40              //!< Advanced RISC Machines ARM
#define EM_FAKE_ALPHA       41              //!< Digital Alpha
#define EM_SH               42              //!< Hitachi SH
#define EM_SPARCV9          43              //!< SPARC Version 9
#define EM_TRICORE          44              //!< Siemens TriCore embedded processor
#define EM_ARC              45              //!< Argonaut RISC Core, Argonaut Technologies Inc
#define EM_H8_300           46              //!< Hitachi H8/300
#define EM_H8_300H          47              //!< Hitachi H8/300H
#define EM_H8S              48              //!< Hitachi H8S
#define EM_H8_500           49              //!< Hitachi H8/500
#define EM_IA_64            50              //!< Intel IA-64 processor architecture
#define EM_MIPS_X           51              //!< Stanford MIPS-X
#define EM_COLDFIRE         52              //!< Motorola ColdFire
#define EM_68HC12           53              //!< Motorola M68HC12
#define EM_MMA              54              //!< Fujitsu MMA Multimedia Accelerator
#define EM_PCP              55              //!< Siemens PCP
#define EM_NCPU             56              //!< Sony nCPU embedded RISC processor
#define EM_NDR1             57              //!< Denso NDR1 microprocessor
#define EM_STARCORE         58              //!< Motorola Start*Core processor
#define EM_ME16             59              //!< Toyota ME16 processor
#define EM_ST100            60              //!< STMicroelectronics ST100 processor
#define EM_TINYJ            61              //!< Advanced Logic Corp. TinyJ embedded processor family
#define EM_X86_64           62              //!< AMD x86-64 architecture
#define EM_PDSP             63              //!< Sony DSP Processor
#define EM_PDP10            64              //!< Digital Equipment Corp. PDP-10
#define EM_PDP11            65              //!< Digital Equipment Corp. PDP-11
#define EM_FX66             66              //!< Siemens FX66 microcontroller
#define EM_ST9PLUS          67              //!< STMicroelectronics ST9+ 8/16 bit microcontroller
#define EM_ST7              68              //!< STMicroelectronics ST7 8-bit microcontroller
#define EM_68HC16           69              //!< Motorola MC68HC16 Microcontroller
#define EM_68HC11           70              //!< Motorola MC68HC11 Microcontroller
#define EM_68HC08           71              //!< Motorola MC68HC08 Microcontroller
#define EM_68HC05           72              //!< Motorola MC68HC05 Microcontroller
#define EM_SVX              73              //!< Silicon Graphics SVx
#define EM_ST19             74              //!< STMicroelectronics ST19 8-bit microcontroller
#define EM_VAX              75              //!< Digital VAX
#define EM_CRIS             76              //!< Axis Communications 32-bit embedded processor
#define EM_JAVELIN          77              //!< Infifineon Technologies 32-bit embedded processor
#define EM_FIREPATH         78              //!< Element 14 64-bit DSP Processor
#define EM_ZSP              79              //!< LSI Logic 16-bit DSP Processor
#define EM_MMIX             80              //!< Donald Knuth's educational 64-bit processor
#define EM_HUANY            81              //!< Harvard University machine-independent object files
#define EM_PRISM            82              //!< SiTera Prism
#define EM_AVR              83              //!< Atmel AVR 8-bit microcontroller
#define EM_FR30             84              //!< Fujitsu FR30
#define EM_D10V             85              //!< Mitsubishi D10V
#define EM_D30V             86              //!< Mitsubishi D30V
#define EM_V850             87              //!< NEC v850
#define EM_M32R             88              //!< Mitsubishi M32R
#define EM_MN10300          89              //!< Matsushita MN10300
#define EM_MN10200          90              //!< Matsushita MN10200
#define EM_PJ               91              //!< picoJava
#define EM_OPENRISC         92              //!< OpenRISC 32-bit embedded processor
#define EM_ARC_A5           93              //!< ARC International ARCompact processor (old spelling/synonym: EM_ARC_A5)
#define EM_XTENSA           94              //!< Tensilica Xtensa Architecture
#define EM_VIDEOCORE        95              //!< Alphamosaic VideoCore processor
#define EM_TMM_GPP          96              //!< Thompson Multimedia General Purpose Processor
#define EM_NS32K            97              //!< National Semiconductor 32000 series
#define EM_TPC              98              //!< Tenor Network TPC processor
#define EM_SNP1K            99              //!< Trebia SNP 1000 processor
#define EM_ST200            100             //!< STMicroelectronics (www.st.com) ST200 microcontroller
#define EM_IP2K             101             //!< Ubicom IP2xxx microcontroller family
#define EM_MAX              102             //!< MAX Processor
#define EM_CR               103             //!< National Semiconductor CompactRISC microprocessor
#define EM_F2MC16           104             //!< Fujitsu F2MC16
#define EM_MSP430           105             //!< Texaxas Instruments embedded microcontroller msp430
#define EM_BLACKFIN         106             //!< Analog Devices Blackfin (DSP) processor
#define EM_SE_C33           107             //!< S1C33 Family of Seiko Epspson processors
#define EM_SEP              108             //!< Sharp embedded microprocessor
#define EM_ARCA             109             //!< Arca RISC Microprocessor
#define EM_UNICORE          110             //!< Microprocessor series from PKU-Unity Ltd. and MPRC of Peking University
#define EM_EXCESS           111             //!< eXcess: 16/32/64-bit configurable embedded CPU
#define EM_DXP              112             //!< Icera Semiconductor Inc. Deep Execution Processor
#define EM_ALTERA_NIOS2     113             //!< Altera Nios II soft-core processor
#define EM_CRX              114             //!< National Semiconductor CompactRISC CRX microprocessor
#define EM_XGATE            115             //!< Motorola XGATE embedded processor
#define EM_C166             116             //!< Infifineon C16x/XC16x processor
#define EM_M16C             117             //!< Renesas M16C series microprocessors
#define EM_DSPIC30F         118             //!< Microchip Technology dsPIC30F Digital Signal Controller
#define EM_CE               119             //!< Freescale Communication Engine RISC core
#define EM_M32C             120             //!< Renesas M32C series microprocessors
#define EM_TSK3000          131             //!< Altium TSK3000 core
#define EM_RS08             132             //!< Freescale RS08 embedded processor
#define EM_ECOG2            134             //!< Cyan Technology eCOG2 microprocessor
#define EM_SCORE7           135             //!< Sunplus S+core7 RISC processor
#define EM_DSP24            136             //!< New Japan Radio (NJR) 24-bit DSP Processor
#define EM_VIDEOCORE3       137             //!< Broadcom VideoCore III processor
#define EM_LATTICEMICO32    138             //!< RISC processor for Lattice FPGA architecture
#define EM_SE_C17           139             //!< Seiko Epspson C17 family
#define EM_TI_C6000         140             //!< The Texaxas Instruments TMS320C6000 DSP family
#define EM_TI_C2000         141             //!< The Texaxas Instruments TMS320C2000 DSP family
#define EM_TI_C5500         142             //!< The Texaxas Instruments TMS320C55x DSP family
#define EM_MMDSP_PLUS       160             //!< STMicroelectronics 64bit VLIW Data Signal Processor
#define EM_CYPRESS_M8C      161             //!< Cypress M8C microprocessor
#define EM_R32C             162             //!< Renesas R32C series microprocessors
#define EM_TRIMEDIA         163             //!< NXP Semiconductors TriMedia architecture family
#define EM_QDSP6            164             //!< QUALCOMM DSP6 Processor
#define EM_8051             165             //!< Intel 8051 and variants
#define EM_STXP7X           166             //!< STMicroelectronics STxP7x family of configurable and extensible RISC processors
#define EM_NDS32            167             //!< Andes Technology compact code size embedded RISC processor family
#define EM_ECOG1            168             //!< Cyan Technology eCOG1X family
#define EM_ECOG1X           168             //!< Cyan Technology eCOG1X family
#define EM_MAXQ30           169             //!< Dallas Semiconductor MAXQ30 Core Micro-controllers
#define EM_XIMO16           170             //!< New Japan Radio (NJR) 16-bit DSP Processor
#define EM_MANIK            171             //!< M2000 Reconfigurable RISC Microprocessor
#define EM_CRAYNV2          172             //!< Cray Inc. NV2 vector architecture
#define EM_RX               173             //!< Renesas RX family
#define EM_METAG            174             //!< Imagination Technologies META processor architecture
#define EM_MCST_ELBRUS      175             //!< MCST Elbrus general purpose hardware architecture
#define EM_ECOG16           176             //!< Cyan Technology eCOG16 family
#define EM_CR16             177             //!< National Semiconductor CompactRISC CR16 16-bit microprocessor
#define EM_ETPU             178             //!< Freescale Extended Time Processing Unit
#define EM_SLE9X            179             //!< Infifineon Technologies SLE9X core
#define EM_AVR32            185             //!< Atmel Corporation 32-bit microprocessor family
#define EM_STM8             186             //!< STMicroeletronics STM8 8-bit microcontroller
#define EM_TILE64           187             //!< Tilera TILE64 multicore architecture family
#define EM_TILEPRO          188             //!< Tilera TILEPro multicore architecture family
#define EM_MICROBLAZE       189             //!< Xilinx MicroBlaze 32-bit RISC soft processor core
#define EM_CUDA             190             //!< NVIDIA CUDA architecture
#define EM_TILEGX           191             //!< Tilera TILE-Gx multicore architecture family

/*
 * e_version (version)
 */
#define EV_NONE             0               //!< Invalid version
#define EV_CURRENT          1               //!< Current version


/*********************************************
 * Section
 *********************************************/
typedef struct
{
  Elf32_Word            sh_name;                    //!< This member specifies the name of the section
  Elf32_Word            sh_type;                    //!< This member categorizes the section's contents and semantics
  Elf32_Word            sh_flags;                   //!< Sections support 1-bit flags that describe miscellaneous attributes
  Elf32_Addr            sh_addr;                    //!< If the section will appear in the memory image of a process, this member gives the address at which the section's first byte should reside
  Elf32_Off             sh_offset;                  //!< This member's value gives the byte offset from the beginning of the file to the first byte in the section
  Elf32_Word            sh_size;                    //!< This member gives the section's size in bytes
  Elf32_Word            sh_link;                    //!< This member holds a section header table index link, whose interpretation depends on the section type
  Elf32_Word            sh_info;                    //!< This member holds extra information, whose interpretation depends on the section type
  Elf32_Word            sh_addralign;               //!< Some sections have address alignment constraints
  Elf32_Word            sh_entsize;                 //!< Some sections hold a table of fixed-size entries, such as a symbol table
} Elf32_Shdr;       //!< 32bit Section header

typedef struct
{
  Elf64_Word            sh_name;                    //!< This member specifies the name of the section
  Elf64_Word            sh_type;                    //!< This member categorizes the section's contents and semantics
  Elf64_Xword           sh_flags;                   //!< Sections support 1-bit flags that describe miscellaneous attributes
  Elf64_Addr            sh_addr;                    //!< If the section will appear in the memory image of a process, this member gives the address at which the section's first byte should reside
  Elf64_Off             sh_offset;                  //!< This member's value gives the byte offset from the beginning of the file to the first byte in the section
  Elf64_Xword           sh_size;                    //!< This member gives the section's size in bytes
  Elf64_Word            sh_link;                    //!< This member holds a section header table index link, whose interpretation depends on the section type
  Elf64_Word            sh_info;                    //!< This member holds extra information, whose interpretation depends on the section type
  Elf64_Xword           sh_addralign;               //!< Some sections have address alignment constraints
  Elf64_Xword           sh_entsize;                 //!< Some sections hold a table of fixed-size entries, such as a symbol table
} Elf64_Shdr;       //!< 64bit Section header

/*
 * Special Section Indexes
 */
#define SHN_UNDEF           0                   //!< This value marks an undefined, missing, irrelevant, or otherwise meaningless section reference
#define SHN_LORESERVE       0xff00              //!< This value specifies the lower bound of the range of reserved indexes
#define SHN_LOPROC          0xff00              //!< Values in this inclusive range are reserved for processor-specific semantics
#define SHN_HIPROC          0xff1f              //!< Values in this inclusive range are reserved for processor-specific semantics
#define SHN_LOOS            0xff20              //!< Values in this inclusive range are reserved for operating system-specific semantics
#define SHN_HIOS            0xff3f              //!< Values in this inclusive range are reserved for operating system-specific semantics
#define SHN_ABS             0xfff1              //!< This value specifies absolute values for the corresponding reference
#define SHN_COMMON          0xfff2              //!< Symbols defined relative to this section are common symbols
#define SHN_XINDEX          0xffff              //!< This value is an escape value
#define SHN_HIRESERVE       0xffff              //!< This value specifies the upper bound of the range of reserved indexes

/*
 * sh_type
 */
#define SHT_NULL            0                   //!< This value marks the section header as inactive
#define SHT_PROGBITS        1                   //!< The section holds information defined by the program
#define SHT_SYMTAB          2                   //!< These sections hold a symbol table
#define SHT_STRTAB          3                   //!< The section holds a string table
#define SHT_RELA            4                   //!< The section holds relocation entries with explicit addends, such as type Elf32_Rela for the 32-bit class of object files or type Elf64_Rela for the 64-bit class of object files
#define SHT_HASH            5                   //!< The section holds a symbol hash table
#define SHT_DYNAMIC         6                   //!< The section holds information for dynamic linking
#define SHT_NOTE            7                   //!< The section holds information that marks the file in some way
#define SHT_NOBITS          8                   //!< A section of this type occupies no space in the file but otherwise resembles SHT_PROGBITS
#define SHT_REL             9                   //!< The section holds relocation entries without explicit addends, such as type Elf32_Rel for the 32-bit class of object files or type Elf64_Rel for the 64-bit class of object files
#define SHT_SHLIB           10                  //!< This section type is reserved but has unspecified semantics
#define SHT_DYNSYM          11                  //!<
#define SHT_INIT_ARRAY      14                  //!< This section contains an array of pointers to initialization functions
#define SHT_FINI_ARRAY      15                  //!< This section contains an array of pointers to termination functions
#define SHT_PREINIT_ARRAY   16                  //!< This section contains an array of pointers to functions that are invoked before all other initialization functions
#define SHT_GROUP           17                  //!< This section defines a section group
#define SHT_SYMTAB_SHNDX    18                  //!< This section is associated with a section of type SHT_SYMTAB and is required if any of the section header indexes referenced by that symbol table contain the escape value SHN_XINDEX
#define SHT_LOOS            0x60000000          //!< Values in this inclusive range are reserved for operating system-specific semantics
#define SHT_HIOS            0x6fffffff          //!< Values in this inclusive range are reserved for operating system-specific semantics
#define SHT_LOPROC          0x70000000          //!< Values in this inclusive range are reserved for processor-specific semantics
#define SHT_HIPROC          0x7fffffff          //!< Values in this inclusive range are reserved for processor-specific semantics
#define SHT_LOUSER          0x80000000          //!< This value specifies the upper bound of the range of indexes reserved for application programs
#define SHT_HIUSER          0x8fffffff          //!< This value specifies the upper bound of the range of indexes reserved for application programs

/*
 * sh_flags
 */
#define SHF_WRITE               0x1             //!< The section contains data that should be writable during process execution
#define SHF_ALLOC               0x2             //!< The section occupies memory during process execution
#define SHF_EXECINSTR           0x4             //!< The section contains executable machine instructions
#define SHF_MERGE               0x10            //!< The data in the section may be merged to eliminate duplication
#define SHF_STRINGS             0x20            //!< The data elements in the section consist of null-terminated character strings
#define SHF_INFO_LINK           0x40            //!< The sh_info field of this section header holds a section header table index
#define SHF_LINK_ORDER          0x80            //!< This flag adds special ordering requirements for link editors
#define SHF_OS_NONCONFORMING    0x100           //!< This section requires special OS-specific processing (beyond the standard linking rules) to avoid incorrect behavior
#define SHF_GROUP               0x200           //!< This section is a member (perhaps the only one) of a section group
#define SHF_TLS                 0x400           //!< This section holds Thread-Local Storage, meaning that each separate execution flow has its own distinct instance of this data
#define SHF_MASKOS              0x0ff00000      //!< All bits included in this mask are reserved for operating system-specific semantics
#define SHF_MASKPROC            0xf0000000      //!< All bits included in this mask are reserved for processor-specific semantics


/*********************************************
 * Symbol
 *********************************************/
typedef struct
{
    Elf32_Word          st_name;                //!< This member holds an index into the object file's symbol string table, which holds the character representations of the symbol names
    Elf32_Addr          st_value;               //!< This member gives the value of the associated symbol
    Elf32_Word          st_size;                //!< Many symbols have associated sizes
    unsigned char       st_info;                //!< This member specifies the symbol's type and binding attributes
    unsigned char       st_other;               //!< This member currently specifies a symbol's visibility
    Elf32_Section       st_shndx;               //!< Every symbol table entry is defined in relation to some section
} Elf32_Sym;

typedef struct
{
    Elf64_Word          st_name;                //!< This member holds an index into the object file's symbol string table, which holds the character representations of the symbol names
    unsigned char       st_info;                //!< This member specifies the symbol's type and binding attributes
    unsigned char       st_other;               //!< This member currently specifies a symbol's visibility
    Elf64_Section       st_shndx;               //!< Every symbol table entry is defined in relation to some section
    Elf64_Addr          st_value;               //!< This member gives the value of the associated symbol
    Elf64_Xword         st_size;                //!< Many symbols have associated sizes
} Elf64_Sym;

/*
 * st_info
 */
#define ELF32_ST_BIND(i)            ((i)>>4)
#define ELF32_ST_TYPE(i)            ((i)&0xf)
#define ELF32_ST_INFO(b,t)          (((b)<<4)+((t)&0xf))

#define ELF64_ST_BIND(i)            ((i)>>4)
#define ELF64_ST_TYPE(i)            ((i)&0xf)
#define ELF64_ST_INFO(b,t)          (((b)<<4)+((t)&0xf))


/* st_info (symbol binding) */
#define STB_LOCAL               0           //!< Local symbols are not visible outside the object file containing their definition
#define STB_GLOBAL              1           //!< Global symbols are visible to all object files being combined
#define STB_WEAK                2           //!< Weak symbols resemble global symbols, but their definitions have lower precedence
#define STB_LOOS                10          //!< Values in this inclusive range are reserved for operating system-specific semantics
#define STB_HIOS                12          //!< Values in this inclusive range are reserved for operating system-specific semantics
#define STB_LOPROC              13          //!< Values in this inclusive range are reserved for processor-specific semantics
#define STB_HIPROC              15          //!< Values in this inclusive range are reserved for processor-specific semantics

/* st_info (symbol type)  */
#define STT_NOTYPE              0           //!< The symbol's type is not specified
#define STT_OBJECT              1           //!< The symbol is associated with a data object, such as a variable, an array, and so on
#define STT_FUNC                2           //!< The symbol is associated with a function or other executable code
#define STT_SECTION             3           //!< The symbol is associated with a section
#define STT_FILE                4           //!< Conventionally, the symbol's name gives the name of the source file associated with the object file
#define STT_COMMON              5           //!< The symbol labels an uninitialized common block
#define STT_TLS                 6           //!<  The symbol specifies a Thread-Local Storage entity
#define STT_LOOS                10          //!< Values in this inclusive range are reserved for operating system-specific semantics
#define STT_HIOS                12          //!< Values in this inclusive range are reserved for operating system-specific semantics
#define STT_LOPROC              13          //!< Values in this inclusive range are reserved for processor-specific semantics
#define STT_HIPROC              15          //!< Values in this inclusive range are reserved for processor-specific semantics

/*
 * st_other
 */
#define ELF32_ST_VISIBILITY(o)          ((o)&0x3)
#define ELF64_ST_VISIBILITY(o)          ((o)&0x3)


#define STV_DEFAULT             0         //!< The visibility of symbols with the STV_DEFAULT attribute is as specified by the symbol's binding type
#define STV_INTERNAL            1         //!< A symbol defined in the current component is protected if it is visible in other components but not preemptable, meaning that any reference to such a symbol from within the defining component must be resolved to the definition in that component, even if there is a definition in another component that would preempt by the default rules
#define STV_HIDDEN              2         //!< A symbol defined in the current component is hidden if its name is not visible to other components
#define STV_PROTECTED           3         //!< The meaning of this visibility attribute may be defined by processor supplements to further constrain hidden symbols


/*********************************************
 * Relocation
 *********************************************/
typedef struct
{
    Elf32_Addr          r_offset;           //!< This member gives the location at which to apply the relocation action
    Elf32_Word          r_info;             //!< This member gives both the symbol table index with respect to which the relocation must be made, and the type of relocation to apply
} Elf32_Rel;        //!< 32bits Relocation Entries

typedef struct
{
    Elf64_Addr          r_offset;           //!< This member gives the location at which to apply the relocation action
    Elf64_Xword         r_info;             //!< This member gives both the symbol table index with respect to which the relocation must be made, and the type of relocation to apply
} Elf64_Rel;        //!< 32bits Relocation Entries

typedef struct
{
    Elf32_Addr          r_offset;           //!< This member gives the location at which to apply the relocation action
    Elf32_Word          r_info;             //!< This member gives both the symbol table index with respect to which the relocation must be made, and the type of relocation to apply
    Elf32_Sword         r_addend;           //!< This member specifies a constant addend used to compute the value to be stored into the relocatable field
} Elf32_Rela;        //!< 32bits Relocation Addend Entries

typedef struct
{
    Elf64_Addr          r_offset;           //!< This member gives the location at which to apply the relocation action
    Elf64_Xword         r_info;             //!< This member gives both the symbol table index with respect to which the relocation must be made, and the type of relocation to apply
    Elf64_Sxword        r_addend;           //!< This member specifies a constant addend used to compute the value to be stored into the relocatable field
} Elf64_Rela;        //!< 32bits Relocation Addend Entries


/*
 * r_info
 */
#define ELF32_R_SYM(i)              ((i)>>8)
#define ELF32_R_TYPE(i)             ((unsigned char)(i))
#define ELF32_R_INFO(s,t)           (((s)<<8)+(unsigned char)(t))

#define ELF64_R_SYM(i)              ((i)>>32)
#define ELF64_R_TYPE(i)             ((i)&0xffffffffL)
#define ELF64_R_INFO(s,t)           (((s)<<32)+((t)&0xffffffffL))



/*********************************************
 * Program
 *********************************************/
typedef struct
{
    Elf32_Word          p_type;             //!< This member tells what kind of segment this array element describes or how to interpret the array element's information
    Elf32_Off           p_offset;           //!< This member gives the offset from the beginning of the file at which the first byte of the segment resides
    Elf32_Addr          p_vaddr;            //!< This member gives the virtual address at which the first byte of the segment resides in memory
    Elf32_Addr          p_paddr;            //!< On systems for which physical addressing is relevant, this member is reserved for the segment's physical address
    Elf32_Word          p_filesz;           //!< This member gives the number of bytes in the file image of the segment; it may be zero
    Elf32_Word          p_memsz;            //!< This member gives the number of bytes in the memory image of the segment; it may be zero
    Elf32_Word          p_flags;            //!< This member gives flags relevant to the segment
    Elf32_Word          p_align;            //!< As ``Program Loading'' describes in this chapter of the processor supplement, loadable process segments must have congruent values for p_vaddr and p_offset, modulo the page size
} Elf32_Phdr;       //!< 32bits Program header

typedef struct
{
    Elf64_Word          p_type;             //!< This member tells what kind of segment this array element describes or how to interpret the array element's information
    Elf64_Word          p_flags;            //!< This member gives flags relevant to the segment
    Elf64_Off           p_offset;           //!< This member gives the offset from the beginning of the file at which the first byte of the segment resides
    Elf64_Addr          p_vaddr;            //!< This member gives the virtual address at which the first byte of the segment resides in memory
    Elf64_Addr          p_paddr;            //!< On systems for which physical addressing is relevant, this member is reserved for the segment's physical address
    Elf64_Xword         p_filesz;           //!< This member gives the number of bytes in the file image of the segment; it may be zero
    Elf64_Xword         p_memsz;            //!< This member gives the number of bytes in the memory image of the segment; it may be zero
    Elf64_Xword         p_align;            //!< As ``Program Loading'' describes in this chapter of the processor supplement, loadable process segments must have congruent values for p_vaddr and p_offset, modulo the page size
} Elf64_Phdr;       //!< 64bits Program header

/*
 * p_type
 */
#define PT_NULL                 0                   //!< The array element is unused; other members' values are undefined
#define PT_LOAD                 1                   //!< The array element specifies a loadable segment, described by p_filesz and p_memsz
#define PT_DYNAMIC              2                   //!< The array element specifies dynamic linking information
#define PT_INTERP               3                   //!< The array element specifies the location and size of a null-terminated path name to invoke as an interpreter
#define PT_NOTE                 4                   //!< The array element specifies the location and size of auxiliary information
#define PT_SHLIB                5                   //!< This segment type is reserved but has unspecified semantics
#define PT_PHDR                 6                   //!< The array element, if present, specifies the location and size of the program header table itself, both in the file and in the memory image of the program
#define PT_TLS                  7                   //!< The array element specifies the Thread-Local Storage template
#define PT_LOOS                 0x60000000          //!< Values in this inclusive range are reserved for operating system-specific semantics
#define PT_HIOS                 0x6fffffff          //!< Values in this inclusive range are reserved for operating system-specific semantics
#define PT_LOPROC               0x70000000          //!< Values in this inclusive range are reserved for processor-specific semantics
#define PT_HIPROC               0x7fffffff          //!< Values in this inclusive range are reserved for processor-specific semantics

/*
 * p_flags
 */
#define PF_X                    (1 << 0)            //!< Execute
#define PF_W                    (1 << 1)            //!< Write
#define PF_R                    (1 << 2)            //!< Read
#define PF_MASKOS               0x0ff00000          //!< Unspecified
#define PF_MASKPROC             0xf0000000          //!< Unspecified

#endif
