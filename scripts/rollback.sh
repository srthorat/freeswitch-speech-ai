#!/bin/bash
#
# Rollback FreeSWITCH to a previous backup
#
# Usage: ./rollback.sh [OPTIONS]
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Defaults
FS_PREFIX="/usr/local/freeswitch"
BACKUP_ROOT="/var/backups/freeswitch"
BACKUP_FILE=""
AUTO_YES=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --freeswitch-prefix)
            FS_PREFIX="$2"
            shift 2
            ;;
        --backup-dir)
            BACKUP_ROOT="$2"
            shift 2
            ;;
        --backup)
            BACKUP_FILE="$2"
            shift 2
            ;;
        --yes)
            AUTO_YES=true
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --freeswitch-prefix PATH  FreeSWITCH installation directory (default: /usr/local/freeswitch)"
            echo "  --backup-dir PATH         Backup root directory (default: /var/backups/freeswitch)"
            echo "  --backup FILE             Specific backup file or directory to restore"
            echo "  --yes                     Skip confirmation prompts"
            echo "  --help                    Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "========================================="
echo "FreeSWITCH Rollback"
echo "========================================="
echo ""

# Check permissions
if [ "$EUID" -ne 0 ]; then
    echo -e "${YELLOW}⚠ Not running as root${NC}"
    echo "  Rollback requires root privileges. Run with sudo."
    exit 1
fi

# If no backup specified, list available backups
if [ -z "$BACKUP_FILE" ]; then
    echo "Available backups:"
    echo ""

    if [ ! -d "$BACKUP_ROOT" ] || [ -z "$(ls -A $BACKUP_ROOT 2>/dev/null)" ]; then
        echo -e "${RED}✗ No backups found in $BACKUP_ROOT${NC}"
        exit 1
    fi

    # List backups with numbers
    BACKUPS=()
    INDEX=1
    for backup in $(ls -t "$BACKUP_ROOT"/backup-* 2>/dev/null); do
        BACKUP_NAME=$(basename "$backup")
        BACKUP_SIZE=$(du -sh "$backup" | cut -f1)
        BACKUP_DATE=$(echo "$BACKUP_NAME" | sed 's/backup-//' | sed 's/\.tar\.gz//' | sed 's/-/ /')

        echo "  $INDEX. $BACKUP_NAME"
        echo "     Date: $BACKUP_DATE"
        echo "     Size: $BACKUP_SIZE"
        echo ""

        BACKUPS+=("$backup")
        INDEX=$((INDEX + 1))
    done

    # Ask user to select
    if [ "$AUTO_YES" = false ]; then
        echo -n "Select backup number to restore (1-${#BACKUPS[@]}), or 'q' to quit: "
        read -r SELECTION

        if [ "$SELECTION" = "q" ] || [ "$SELECTION" = "Q" ]; then
            echo "Aborted."
            exit 0
        fi

        if ! [[ "$SELECTION" =~ ^[0-9]+$ ]] || [ "$SELECTION" -lt 1 ] || [ "$SELECTION" -gt ${#BACKUPS[@]} ]; then
            echo -e "${RED}✗ Invalid selection${NC}"
            exit 1
        fi

        BACKUP_FILE="${BACKUPS[$((SELECTION-1))]}"
    else
        # Auto mode - use most recent backup
        BACKUP_FILE="${BACKUPS[0]}"
        echo "Using most recent backup (auto mode): $(basename $BACKUP_FILE)"
    fi
fi

# Verify backup exists
if [ ! -e "$BACKUP_FILE" ]; then
    echo -e "${RED}✗ Backup not found: $BACKUP_FILE${NC}"
    exit 1
fi

echo ""
echo "Rollback Plan:"
echo "  From: $BACKUP_FILE"
echo "  To:   $FS_PREFIX"
echo ""

# Warn about data loss
echo -e "${YELLOW}⚠ WARNING: This will replace the current FreeSWITCH installation${NC}"
echo "  Current installation will be lost unless you create a backup first."
echo ""

# Confirmation
if [ "$AUTO_YES" = false ]; then
    read -p "Do you want to create a backup of the current installation first? (Y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Nn]$ ]]; then
        echo "Creating backup of current installation..."
        if [ -x "$(dirname $0)/backup.sh" ]; then
            "$(dirname $0)/backup.sh" --freeswitch-prefix "$FS_PREFIX" --backup-dir "$BACKUP_ROOT" --name "pre-rollback-$(date +%Y%m%d-%H%M%S)"
        else
            echo -e "${YELLOW}⚠ backup.sh not found, skipping pre-rollback backup${NC}"
        fi
        echo ""
    fi

    read -p "Proceed with rollback? This will overwrite the current installation. (type 'yes' to confirm): " -r
    if [ "$REPLY" != "yes" ]; then
        echo "Aborted."
        exit 0
    fi
fi

echo ""
echo "Starting rollback..."
echo ""

# Stop FreeSWITCH if running
if systemctl is-active --quiet freeswitch 2>/dev/null; then
    echo -n "  Stopping FreeSWITCH... "
    systemctl stop freeswitch
    echo -e "${GREEN}✓${NC}"
fi

# Extract backup if it's compressed
RESTORE_DIR=""
if [[ "$BACKUP_FILE" == *.tar.gz ]]; then
    echo -n "  Extracting backup... "
    TEMP_DIR=$(mktemp -d)
    tar -xzf "$BACKUP_FILE" -C "$TEMP_DIR"
    RESTORE_DIR="$TEMP_DIR/$(basename $BACKUP_FILE .tar.gz)"
    echo -e "${GREEN}✓${NC}"
else
    RESTORE_DIR="$BACKUP_FILE"
fi

# Verify extracted backup structure
if [ ! -d "$RESTORE_DIR" ]; then
    echo -e "${RED}✗ Invalid backup structure${NC}"
    exit 1
fi

# Remove current installation
if [ -d "$FS_PREFIX" ]; then
    echo -n "  Removing current installation... "
    rm -rf "$FS_PREFIX"
    echo -e "${GREEN}✓${NC}"
fi

# Restore FreeSWITCH
echo -n "  Restoring FreeSWITCH... "
mkdir -p "$(dirname $FS_PREFIX)"
cp -a "$RESTORE_DIR" "$FS_PREFIX"
echo -e "${GREEN}✓${NC}"

# Restore systemd service if exists
if [ -f "${RESTORE_DIR}/freeswitch.service" ]; then
    echo -n "  Restoring systemd service... "
    cp -a "${RESTORE_DIR}/freeswitch.service" "/etc/systemd/system/"
    systemctl daemon-reload
    echo -e "${GREEN}✓${NC}"
fi

# Cleanup temporary extraction
if [[ "$BACKUP_FILE" == *.tar.gz ]]; then
    rm -rf "$TEMP_DIR"
fi

# Run ldconfig
echo -n "  Updating library cache... "
ldconfig
echo -e "${GREEN}✓${NC}"

# Start FreeSWITCH
echo -n "  Starting FreeSWITCH... "
if systemctl start freeswitch 2>/dev/null; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${YELLOW}⚠ Could not start via systemd${NC}"
    echo "    Start manually: $FS_PREFIX/bin/freeswitch -nc"
fi

echo ""
echo -e "${GREEN}✓ Rollback completed successfully${NC}"
echo ""
echo "Next steps:"
echo "  1. Verify installation: ./scripts/status.sh"
echo "  2. Run health check:    ./scripts/health-check.sh"
echo ""

exit 0
