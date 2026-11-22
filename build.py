import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import time

build_tools = ["nasm", "clang", "lld"]
emulation_tools = ["qemu-system-x86_64"]

bootloader_sources = ['src/boot/stage1.asm', 'src/boot/stage2.asm']

kernel_asm_sources_and_outputs = [
    ('src/interrupt/isr.asm', 'out/isr.o'),
    ('src/io/io.asm', 'out/io.o'),
    ('src/memory/memory.asm', 'out/memory.o'),
    ('src/vga/vga.asm', 'out/vga.o')
]

kernel_c_sources = ['src/kernel.c', 'src/vga/vga.c', 'src/common.c', 'src/memory/phys_memory_map.c', 'src/memory/frame_allocator.c',
                  'src/memory/page_table.c',
                  'src/interrupt/handlers.c', 'src/interrupt/idt.c', 'src/io/usb_controller.c', 'src/memory/arena_allocator.c',
                    'src/io/usb_device.c', 'src/io/usb_device_manager.c', 'src/io/usb_interface.c',
                                                                          'src/io/usb_interface_endpoint.c']

linker_script_template = 'linker_template.ld'
mem_layout_path = 'out/mem_layout.inc'

stack_limit = 1 << 20
stack_size = 1 << 24
stack_base = stack_limit + stack_size
text_addr = stack_base

os_image_path = "out/idaho.img"


def print_green(message):
    print(f"\033[32m{message}\033[0m")

def print_yellow(message):
    print(f"\033[33m{message}\033[0m")

def print_red(message):
    print(f"\033[31m{message}\033[0m")

def parse_memory_size(input_size):
    input_size = input_size.strip()
    if input_size.endswith(("KiB", "MiB", "GiB")):
        input_size = input_size[:-2]
    elif input_size.endswith(("KB", "MB", "GB")):
        input_size = input_size[:-1]
    elif input_size.endswith(("Ki", "Mi", "Gi")):
        input_size = input_size[:-1]

    input_suffix = input_size.lstrip("0123456789").lstrip()
    if input_suffix:
        number = input_size[:-len(input_suffix)].strip()
    else:
        number = input_size.strip()

    if not number.isascii() or not number.isdigit():
        print_red("Invalid memory size.")
        raise argparse.ArgumentTypeError()

    if input_suffix:
        if input_suffix in {"K", "M", "G"}:
            si_suffix = input_suffix
        else:   # invalid suffix
            print_red("Invalid memory size.")
            raise argparse.ArgumentTypeError()
    else:
        si_suffix = "M" # QEMU interprets no sufix as M, i.e. 4096 -> 4096 MiB

    return f"{number}{si_suffix}"

def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--force-build", action="store_true")

    args, remaining = parser.parse_known_args()

    command_parser = argparse.ArgumentParser()
    subparsers = command_parser.add_subparsers(dest="command")

    subparsers.add_parser("clean")

    run_parser = subparsers.add_parser("run")
    run_parser.add_argument("memory_size", nargs="?", type=parse_memory_size, default="4G")
    run_parser.add_argument("--curses", action="store_true")

    debug_parser = subparsers.add_parser("debug")
    debug_parser.add_argument(
        "debug_command",
        nargs="?",
        default="info registers"
    )
    debug_parser.add_argument("delay", nargs="?", type=int, default=2)

    command_args = command_parser.parse_args(remaining)
    command_args.force_build = args.force_build

    return command_args


def build_is_needed():
    if not os.path.exists(os_image_path):
        return True

    image_mod_time = os.path.getmtime(os_image_path)

    if os.path.getmtime(linker_script_template) > image_mod_time:
        return True

    for root, _, files in os.walk('src'):
        for filename in files:
            src_path = os.path.join(root, filename)

            if os.path.getmtime(src_path) > image_mod_time:
                return True

    return False


def check_tools(needed_tools):
    missing_tools = []

    for tool in needed_tools:
        if not shutil.which(tool):
            missing_tools.append(tool)

    if missing_tools:
        print_red("Following tools are not installed. Install them first.")
        print_red(" ".join(missing_tools))
        return False

    return True


def prepare_linker_script():
    with open(linker_script_template, 'r') as f:
        linker_script_content = f.read()

    linker_script_content = linker_script_content.replace("TEXT_ADDR", f"0x{text_addr:X}")

    linker_script_path = 'out/linker.ld'

    with open(linker_script_path, 'w') as f:
        f.write(linker_script_content)

    return linker_script_path


def assemble_kernel_asm(source, output):
    try:
        subprocess.run(['nasm', '-f', 'elf64', source, '-o', output], check=True)
    except subprocess.CalledProcessError:
        print_red(f"Error assembling kernel source {source}.")
        sys.exit(1)

def compile_kernel(
        c_source_files,
        asm_objects,
        linker_script_path,
        output,
):
    compile_command = [
    "clang",
    "--target=x86_64-unknown-none-elf",
    "-mabi=sysv",
    "-fuse-ld=lld",
    "-std=c11",
    "-ffreestanding",
    "-fno-pie",
    "-fno-pic",
    "-fno-stack-protector",
    "-fno-asynchronous-unwind-tables",
    "-mno-red-zone",
    "-nostdlib",
    "-static",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-s",
    *c_source_files,
    *asm_objects,
    "-T", linker_script_path,
    "-Wl,--oformat=binary",
    "-Wl,-Map,out/kernel.map",
    "-o", output
]

    try:
        subprocess.run(compile_command, check=True)
    except subprocess.CalledProcessError:
        print_red("Error compiling kernel.")
        sys.exit(1)

     # Pad kernel to full sectors
    size = os.path.getsize(output)
    padding = (-size) % 512

    if padding:
        with open(output, "ab") as f:
            f.write(b"\0" * padding)


def parse_kernel_map(symbol_name):
    with open("out/kernel.map", "r") as f:
        for line in f:
            if symbol_name in line:
                value = int(line.split()[0], 16)
                return value

    raise RuntimeError(f"Could not find {symbol_name} in kernel map.")


def write_kernel_layout(kernel_path):
    kernel_file_size = os.path.getsize(kernel_path)
    kernel_sectors = (kernel_file_size + 511) // 512

    kernel_memory_end = parse_kernel_map("__kernel_end")
    kernel_memory_size = kernel_memory_end - text_addr

    kernel_entry_point = parse_kernel_map("kernel_main")

    with open(mem_layout_path, 'w') as f:
        f.write(f"KERNEL_FILE_SIZE equ 0x{kernel_file_size:X}\n")
        f.write(f"KERNEL_BLOB_SECTORS equ 0x{kernel_sectors:X}\n")
        f.write(f"KERNEL_TEXT_ADDR equ 0x{text_addr:X}\n")
        f.write(f"KERNEL_MEMORY_SIZE equ 0x{kernel_memory_size:X}\n")
        f.write(f"STACK_BASE equ 0x{stack_base:X}\n")
        f.write(f"STACK_LIMIT equ 0x{stack_limit:X}\n")
        f.write(f"KERNEL_ENTRY_POINT equ 0x{kernel_entry_point:X}\n")

def assemble_bootloader_stage(source, output):
    try:
        subprocess.run(['nasm', '-f', 'bin', source, '-o', output], check=True)
    except subprocess.CalledProcessError:
        print_red(f"Error assembling {source}.")
        sys.exit(1)

def write_stage2_sectors_count(stage2_path):
    stage2_size = os.path.getsize(stage2_path)
    stage2_sectors = (stage2_size + 511) // 512

    with open(mem_layout_path, 'a') as f:
        f.write(f"STAGE2_SECTORS equ 0x{stage2_sectors:X}\n")


def create_disk_image():
    with open(os_image_path, "wb") as img_file:
        for filename in ["out/bootloader_stage1.bin", "out/bootloader_stage2.bin", "out/kernel.bin"]:
            with open(filename, "rb") as f:
                img_file.write(f.read())

def build():
    print("Compiling to out directory.")
    os.makedirs('out', exist_ok=True)

    print("Preparing linker script... ", end="")
    linker_script_path = prepare_linker_script()
    print(f"Written to {linker_script_path}")

    for (src, output) in kernel_asm_sources_and_outputs:
        print(f"Assembling {src}... ", end="")
        assemble_kernel_asm(src, output)
        print(f"Assembled {output}")

    asm_objects = [path[1] for path in kernel_asm_sources_and_outputs]

    print("Compiling kernel... ", end="")
    compile_kernel(kernel_c_sources, asm_objects , linker_script_path, 'out/kernel.bin')
    print("Compiled to out/kernel.bin")

    write_kernel_layout('out/kernel.bin')

    print("Assembling bootloader stage 2... ", end="")
    assemble_bootloader_stage(bootloader_sources[1], 'out/bootloader_stage2.bin')
    print("Done")

    write_stage2_sectors_count('out/bootloader_stage2.bin')

    print("Assembling bootloader stage 1... ", end="")
    assemble_bootloader_stage(bootloader_sources[0], 'out/bootloader_stage1.bin')
    print("Done")

    print(f"Joining OS files to {os_image_path}...")
    create_disk_image()

    print_green('All done')

def qmp_execute(qmp_stream, command, arguments=None):
    request = {"execute": command}

    if arguments is not None:
        request["arguments"] = arguments

    qmp_stream.write((json.dumps(request) + "\r\n").encode("utf-8"))
    qmp_stream.flush()

    while True:
        response_line = qmp_stream.readline()

        if not response_line:
            raise RuntimeError("QMP has closed connection.")

        response = json.loads(response_line)

        if "event" in response:
            continue

        if "error" in response:
            raise RuntimeError(f"QMP error while executing {command}: {response['error']}")

        if "return" in response:
            return response["return"]

        raise RuntimeError(f"Unexpected QMP response: {response}")

# def qemu_run(qemu_memory_size, use_curses):
#     print(f"Running QEMU with {qemu_memory_size}iB of memory.")
#
#     qmp_socket_path = "/tmp/idaho-os-qmp.sock"
#
#     if os.path.exists(qmp_socket_path):
#         os.remove(qmp_socket_path)
#
#     qemu_command = [
#         'qemu-system-x86_64',
#         '-drive', f'format=raw,file={os_image_path}',
#         '-m', qemu_memory_size,
#         '-device', 'qemu-xhci,id=xhci',
#         '-qmp', f'unix:{qmp_socket_path},server=on,wait=off'
#     ]
#
#     if use_curses:
#         qemu_command += ['-vga', 'std', '-display', 'curses']
#
#     qemu_process = None
#     try:
#         qemu_process = subprocess.Popen(qemu_command)
#         qmp_socket = None
#
#         for _ in range(100):
#             try:
#                 qmp_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
#                 qmp_socket.connect(qmp_socket_path)
#                 break
#             except OSError:
#                 if qmp_socket is not None:
#                     qmp_socket.close()
#
#                 qmp_socket = None
#
#                 if qemu_process.poll() is not None:
#                     raise RuntimeError("QEMU quitted before starting QMP.")
#
#                 time.sleep(0.1)
#
#         if qmp_socket is None:
#             raise RuntimeError("Could not connect to QMP.")
#
#         with qmp_socket:
#             with qmp_socket.makefile("rwb") as qmp_stream:
#                 greeting_line = qmp_stream.readline()
#
#                 if not greeting_line:
#                     raise RuntimeError("QMP has not sent a greeting.")
#
#                 greeting = json.loads(greeting_line)
#
#                 if "QMP" not in greeting:
#                     raise RuntimeError("Invalid QMP greeting.")
#
#                 qmp_execute(qmp_stream, "qmp_capabilities")
#
#                 print("Waiting for the OS to initialize xHCI...")
#                 time.sleep(2.0)
#
#                 qmp_execute(
#                     qmp_stream,
#                     "device_add",
#                     {
#                         "driver": "usb-kbd",
#                         "bus": "xhci.0",
#                         "id": "kbd"
#                     }
#                 )
#
#                 print("USB keyboard connected.")
#
#         qemu_process.wait()
#
#     except KeyboardInterrupt:
#         if qemu_process is not None:
#             qemu_process.terminate()
#             qemu_process.wait()
#
#         print_yellow("Simulation terminated.")
#         sys.exit(0)
#
#     except Exception:
#         if qemu_process is not None and qemu_process.poll() is None:
#             qemu_process.terminate()
#             qemu_process.wait()
#
#         raise
#
#     finally:
#         if os.path.exists(qmp_socket_path):
#             os.remove(qmp_socket_path)

def qemu_run(qemu_memory_size, use_curses):
    print(f"Running QEMU with {qemu_memory_size}iB of memory.")

    qemu_command = [
        'qemu-system-x86_64',
        '-drive', f'format=raw,file={os_image_path}',
        '-m', qemu_memory_size,
        '-device', 'qemu-xhci,id=xhci',
        '-device', 'usb-kbd,bus=xhci.0,id=kbd'
    ]

    if use_curses:
        qemu_command += ['-vga', 'std', '-display', 'curses']

    qemu_process = None
    try:
        qemu_process = subprocess.Popen(qemu_command)
        qemu_process.wait()

    except KeyboardInterrupt:
        if qemu_process is not None:
            qemu_process.terminate()
            qemu_process.wait()

        print_yellow("Simulation terminated.")
        sys.exit(0)

    except Exception:
        if qemu_process is not None and qemu_process.poll() is None:
            qemu_process.terminate()
            qemu_process.wait()

        raise

def qemu_debug(debug_command, delay):
    qemu = subprocess.Popen(
    [
        "qemu-system-x86_64",
        "-drive", f"format=raw,file={os_image_path}",
        "-monitor", "stdio"
    ],
    stdin=subprocess.PIPE,
    text=True
)

    time.sleep(delay)

    qemu.stdin.write(f"{debug_command}\n")
    qemu.stdin.flush()

    qemu.wait()


def clean_all():
    shutil.rmtree('out', ignore_errors=True)


def main():
    args = parse_arguments()

    if args.command == "clean":
        clean_all()
        return

    build_request = args.force_build or build_is_needed()

    if build_request:
        if not check_tools(build_tools):
            return
        build()

    if not build_request and args.command is None:
        print("Nothing to do.")

    if args.command == "run":
        if not check_tools(emulation_tools):
            return
        qemu_run(args.memory_size, args.curses)

    elif args.command == "debug":
        if not check_tools(emulation_tools):
            return
        qemu_debug(args.debug_command, args.delay)

if __name__ == '__main__':
    main()
