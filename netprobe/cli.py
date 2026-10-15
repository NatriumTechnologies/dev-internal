import argparse
import asyncio
import sys
import json
import time
from netprobe.formatting import Colors, banner, print_table
from netprobe.scanner import PortScanner, TOP_100_PORTS
from netprobe.sweep import SubnetSweeper
from netprobe.sniffer import PacketSniffer
from netprobe.banner import BannerGrabber

def parse_args():
    parser = argparse.ArgumentParser(
        prog="netprobe",
        description="High-Speed Asynchronous Network Scanner & Packet Analyzer"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # Command: scan
    scan_p = subparsers.add_parser("scan", help="Scan ports on a target IP or hostname")
    scan_p.add_argument("target", help="Target hostname or IP address (e.g. 127.0.0.1, scanme.nmap.org)")
    scan_p.add_argument("-p", "--ports", help="Port list/range (e.g. '80,443', '1-1000')")
    scan_p.add_argument("--top", type=int, choices=[100], help="Scan top N common ports (default: 100 if -p omitted)")
    scan_p.add_argument("-t", "--timeout", type=float, default=1.2, help="Connection timeout in seconds (default: 1.2)")
    scan_p.add_argument("-c", "--concurrency", type=int, default=256, help="Concurrent connection workers (default: 256)")
    scan_p.add_argument("--no-banner", action="store_true", help="Disable service banner grabbing")
    scan_p.add_argument("--json", action="store_true", help="Output results in JSON format")
    scan_p.add_argument("-o", "--output", help="Save results to a file")

    # Command: sweep
    sweep_p = subparsers.add_parser("sweep", help="Discover live hosts across a subnet range")
    sweep_p.add_argument("cidr", help="Subnet in CIDR format (192.168.1.0/24) or range (192.168.1.1-50)")
    sweep_p.add_argument("-t", "--timeout", type=float, default=0.8, help="Host probe timeout in seconds (default: 0.8)")
    sweep_p.add_argument("-c", "--concurrency", type=int, default=128, help="Worker concurrency (default: 128)")
    sweep_p.add_argument("--json", action="store_true", help="Output results in JSON format")
    sweep_p.add_argument("-o", "--output", help="Save results to a file")

    # Command: sniff
    sniff_p = subparsers.add_parser("sniff", help="Sniff and dissect raw network packets (Admin/Root required)")
    sniff_p.add_argument("-c", "--count", type=int, default=25, help="Number of packets to capture (default: 25)")
    sniff_p.add_argument("-f", "--filter", choices=["tcp", "udp", "icmp"], help="Filter by protocol")
    sniff_p.add_argument("-i", "--interface", help="Interface bind IP (default: primary local IP)")

    # Command: check
    check_p = subparsers.add_parser("check", help="Detailed single-port banner probe")
    check_p.add_argument("target", help="Target host")
    check_p.add_argument("port", type=int, help="Target port")

    return parser.parse_args()

def run_scan(args):
    if not args.json:
        print(banner())

    # Determine port list
    if args.ports:
        ports = PortScanner.parse_ports(args.ports)
    else:
        ports = TOP_100_PORTS

    scanner = PortScanner(
        target=args.target,
        ports=ports,
        timeout=args.timeout,
        concurrency=args.concurrency,
        grab_banners=not args.no_banner
    )

    try:
        resolved = scanner.resolve()
    except Exception as e:
        print(f"{Colors.RED}[!] Could not resolve target '{args.target}': {e}{Colors.RESET}")
        sys.exit(1)

    if not args.json:
        print(f"[*] Target:      {Colors.BOLD}{args.target}{Colors.RESET} ({resolved})")
        print(f"[*] Port Count:  {len(ports)} ports")
        print(f"[*] Concurrency: {args.concurrency} async workers")
        print(f"[*] Scanning...\n")

    t_start = time.perf_counter()

    def progress(done, total):
        if not args.json and sys.stdout.isatty():
            pct = int((done / total) * 100)
            sys.stdout.write(f"\r{Colors.DIM}[Progress: {pct}% ({done}/{total})]{Colors.RESET}")
            sys.stdout.flush()

    results = asyncio.run(scanner.run(progress_cb=progress if not args.json else None))
    elapsed = time.perf_counter() - t_start

    if not args.json:
        sys.stdout.write("\r" + " " * 40 + "\r")
        rows = []
        for r in results:
            port_col = f"{Colors.GREEN}{r.port}/tcp{Colors.RESET}"
            state_col = f"{Colors.GREEN}open{Colors.RESET}"
            rtt_col = f"{r.rtt_ms:.1f}ms"
            service_col = f"{Colors.CYAN}{r.service}{Colors.RESET}"
            banner_col = f"{Colors.DIM}{r.banner}{Colors.RESET}" if r.banner else ""
            rows.append([port_col, state_col, service_col, rtt_col, banner_col])

        print_table(["PORT", "STATE", "SERVICE", "RTT", "BANNER"], rows)
        print(f"\n{Colors.BOLD}Completed:{Colors.RESET} {len(results)} open ports found in {elapsed:.2f}s.")
    else:
        out_data = {
            "target": args.target,
            "ip": resolved,
            "scan_time_sec": round(elapsed, 3),
            "open_ports": [
                {
                    "port": r.port,
                    "service": r.service,
                    "rtt_ms": r.rtt_ms,
                    "banner": r.banner
                }
                for r in results
            ]
        }
        json_str = json.dumps(out_data, indent=2)
        print(json_str)

    if args.output:
        with open(args.output, "w") as f:
            if args.json:
                f.write(json.dumps(out_data, indent=2))
            else:
                for r in results:
                    f.write(f"{r.port} open {r.service} {r.rtt_ms}ms {r.banner}\n")
        print(f"{Colors.DIM}Results saved to {args.output}{Colors.RESET}")

def run_sweep(args):
    if not args.json:
        print(banner())
        print(f"[*] Sweeping subnet: {Colors.BOLD}{args.cidr}{Colors.RESET}")
        print(f"[*] Concurrency:     {args.concurrency} workers\n")

    t_start = time.perf_counter()
    sweeper = SubnetSweeper(args.cidr, timeout=args.timeout, concurrency=args.concurrency)
    results = asyncio.run(sweeper.run())
    elapsed = time.perf_counter() - t_start

    if not args.json:
        rows = []
        for h in results:
            ip_col = f"{Colors.GREEN}{h.ip}{Colors.RESET}"
            host_col = h.hostname or f"{Colors.DIM}(unknown){Colors.RESET}"
            ports_col = ",".join(str(p) for p in h.open_ports)
            rtt_col = f"{h.rtt_ms:.1f}ms"
            rows.append([ip_col, host_col, ports_col, rtt_col])

        print_table(["IP ADDRESS", "HOSTNAME", "OPEN PORTS", "RTT"], rows)
        print(f"\n{Colors.BOLD}Completed:{Colors.RESET} {len(results)} live hosts detected in {elapsed:.2f}s.")
    else:
        out_data = {
            "cidr": args.cidr,
            "sweep_time_sec": round(elapsed, 3),
            "hosts": [
                {
                    "ip": h.ip,
                    "hostname": h.hostname,
                    "open_ports": h.open_ports,
                    "rtt_ms": h.rtt_ms
                }
                for h in results
            ]
        }
        print(json.dumps(out_data, indent=2))

def run_sniff(args):
    print(banner())
    sniffer = PacketSniffer(interface_ip=args.interface, proto_filter=args.filter)

    try:
        sniffer.open()
    except PermissionError as e:
        print(f"{Colors.RED}[!] {e}{Colors.RESET}")
        sys.exit(1)

    print(f"[*] Bound Interface: {Colors.BOLD}{sniffer.interface_ip}{Colors.RESET}")
    print(f"[*] Protocol Filter:  {args.filter.upper() if args.filter else 'ALL'}")
    print(f"[*] Capturing {args.count} packets (Ctrl+C to stop)...\n")

    headers = ["PROTO", "SRC IP:PORT", "DST IP:PORT", "FLAGS", "PAYLOAD"]
    rows = []

    try:
        for p in sniffer.sniff(count=args.count):
            src = f"{p.src_ip}:{p.src_port}" if p.src_port else p.src_ip
            dst = f"{p.dst_ip}:{p.dst_port}" if p.dst_port else p.dst_ip
            proto_color = Colors.CYAN if p.protocol == "TCP" else (Colors.YELLOW if p.protocol == "UDP" else Colors.MAGENTA)
            rows.append([
                f"{proto_color}{p.protocol}{Colors.RESET}",
                src,
                dst,
                f"{Colors.DIM}{p.flags_str}{Colors.RESET}",
                p.payload_preview
            ])
    except KeyboardInterrupt:
        print(f"\n{Colors.YELLOW}[*] Capture stopped by user.{Colors.RESET}")
    finally:
        sniffer.close()

    print_table(headers, rows)

def run_check(args):
    print(banner())
    print(f"[*] Probing {args.target}:{args.port}...")
    t0 = time.perf_counter()
    service, banner_text = asyncio.run(BannerGrabber.grab(args.target, args.port, timeout=3.0))
    rtt = (time.perf_counter() - t0) * 1000.0

    print(f"  Service: {Colors.CYAN}{service}{Colors.RESET}")
    print(f"  RTT:     {rtt:.2f} ms")
    print(f"  Banner:  {Colors.GREEN}{banner_text if banner_text else '(no response banner)'}{Colors.RESET}")

def main():
    args = parse_args()
    if args.command == "scan":
        run_scan(args)
    elif args.command == "sweep":
        run_sweep(args)
    elif args.command == "sniff":
        run_sniff(args)
    elif args.command == "check":
        run_check(args)

if __name__ == "__main__":
    main()
