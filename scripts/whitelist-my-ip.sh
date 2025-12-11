#!/bin/bash
# ============================================================================
# FreeSWITCH Speech AI - Whitelist Current Public IP in fail2ban
# ============================================================================
# This script automatically adds your current public IP to the fail2ban
# ignore list for the freeswitch jail, preventing accidental self-bans.
#
# Usage:
#   sudo ./whitelist-my-ip.sh                    # Auto-detect public IP
#   sudo ./whitelist-my-ip.sh --ip 1.2.3.4       # Specify IP address
#   sudo ./whitelist-my-ip.sh --remove           # Remove auto-detected IP
#   sudo ./whitelist-my-ip.sh --ip 1.2.3.4 --remove  # Remove specific IP
# ============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# Configuration
JAIL_CONF="/etc/fail2ban/jail.d/freeswitch.conf"
REMOVE_MODE=false
MANUAL_IP=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --remove)
            REMOVE_MODE=true
            shift
            ;;
        --ip)
            MANUAL_IP="$2"
            shift 2
            ;;
        --help)
            echo "FreeSWITCH Speech AI - Whitelist IP in fail2ban"
            echo ""
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --ip IP_ADDRESS    Specify IP address (skips auto-detection)"
            echo "  --remove           Remove IP from whitelist instead of adding"
            echo "  --help             Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                           # Auto-detect and add your public IP"
            echo "  $0 --ip 182.70.103.53        # Add specific IP"
            echo "  $0 --remove                  # Remove auto-detected IP"
            echo "  $0 --ip 182.70.103.53 --remove  # Remove specific IP"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Try '$0 --help' for more information."
            exit 1
            ;;
    esac
done

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root${NC}"
    echo "Please run: sudo $0"
    exit 1
fi

# Function to get public IP
get_public_ip() {
    local ip=""
    
    # Try multiple services for reliability
    ip=$(curl -s --max-time 5 https://api.ipify.org 2>/dev/null) || \
    ip=$(curl -s --max-time 5 https://ifconfig.me 2>/dev/null) || \
    ip=$(curl -s --max-time 5 https://icanhazip.com 2>/dev/null) || \
    ip=$(dig +short myip.opendns.com @resolver1.opendns.com 2>/dev/null)
    
    echo "$ip"
}

# Get IP address (manual or auto-detect)
if [ -n "$MANUAL_IP" ]; then
    PUBLIC_IP="$MANUAL_IP"
    echo -e "${CYAN}Using specified IP: $PUBLIC_IP${NC}"
    
    # Validate IP address format
    if ! [[ "$PUBLIC_IP" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]; then
        echo -e "${RED}Error: Invalid IP address format: $PUBLIC_IP${NC}"
        echo "Please provide a valid IPv4 address (e.g., 192.168.1.1)"
        exit 1
    fi
else
    echo -e "${CYAN}Detecting your public IP...${NC}"
    PUBLIC_IP=$(get_public_ip)

    if [ -z "$PUBLIC_IP" ]; then
        echo -e "${RED}Error: Could not detect public IP address${NC}"
        echo "Please check your internet connection or use --ip to specify manually"
        exit 1
    fi

    echo -e "${GREEN}✓ Detected public IP: $PUBLIC_IP${NC}"
fi
echo ""

# Check if jail config exists
if [ ! -f "$JAIL_CONF" ]; then
    echo -e "${RED}Error: fail2ban jail configuration not found at $JAIL_CONF${NC}"
    exit 1
fi

# Read current ignore list
CURRENT_IGNOREIP=$(grep "^ignoreip" "$JAIL_CONF" | sed 's/ignoreip = //')

if [ "$REMOVE_MODE" = true ]; then
    # Remove mode
    if echo "$CURRENT_IGNOREIP" | grep -q "$PUBLIC_IP"; then
        echo -e "${CYAN}Removing $PUBLIC_IP from whitelist...${NC}"
        
        # Remove the IP from the list
        NEW_IGNOREIP=$(echo "$CURRENT_IGNOREIP" | sed "s/\b$PUBLIC_IP\b//g" | sed 's/  */ /g' | sed 's/^ //;s/ $//')
        
        # Update config file
        sed -i "s|^ignoreip = .*|ignoreip = $NEW_IGNOREIP|" "$JAIL_CONF"
        
        echo -e "${GREEN}✓ IP $PUBLIC_IP removed from whitelist${NC}"
    else
        echo -e "${YELLOW}⚠ IP $PUBLIC_IP not found in whitelist${NC}"
    fi
else
    # Add mode
    if echo "$CURRENT_IGNOREIP" | grep -q "$PUBLIC_IP"; then
        echo -e "${GREEN}✓ IP $PUBLIC_IP is already whitelisted${NC}"
        echo ""
        echo "Current whitelist: $CURRENT_IGNOREIP"
        exit 0
    fi
    
    echo -e "${CYAN}Adding $PUBLIC_IP to whitelist...${NC}"
    
    # Add the new IP to the list
    NEW_IGNOREIP="$CURRENT_IGNOREIP $PUBLIC_IP"
    
    # Update config file
    sed -i "s|^ignoreip = .*|ignoreip = $NEW_IGNOREIP|" "$JAIL_CONF"
    
    echo -e "${GREEN}✓ IP $PUBLIC_IP added to whitelist${NC}"
fi

# Display updated configuration
echo ""
echo -e "${BOLD}Updated configuration:${NC}"
grep "^ignoreip" "$JAIL_CONF"

# Reload fail2ban
echo ""
echo -e "${CYAN}Reloading fail2ban freeswitch jail...${NC}"
fail2ban-client reload freeswitch

echo -e "${GREEN}✓ fail2ban reloaded successfully${NC}"
echo ""

# Show current status
echo -e "${BOLD}Current jail status:${NC}"
fail2ban-client status freeswitch

echo ""
echo -e "${GREEN}=============================================${NC}"
echo -e "${GREEN}✓ Complete!${NC}"
echo -e "${GREEN}=============================================${NC}"
echo ""
if [ "$REMOVE_MODE" = true ]; then
    echo "Your public IP ($PUBLIC_IP) has been removed from the whitelist."
else
    echo "Your public IP ($PUBLIC_IP) is now whitelisted."
    echo "You will not be banned by fail2ban for FreeSWITCH."
fi
echo ""
