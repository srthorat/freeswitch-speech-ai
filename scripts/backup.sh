#!/bin/bash
#
# Backup FreeSWITCH installation before updates or changes
#
# Usage: ./backup.sh [OPTIONS]
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
BACKUP_NAME="backup-$(date +%Y%m%d-%H%M%S)"
COMPRESS=true

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
        --name)
            BACKUP_NAME="$2"
            shift 2
            ;;
        --no-compress)
            COMPRESS=false
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --freeswitch-prefix PATH  FreeSWITCH installation directory (default: /usr/local/freeswitch)"
            echo "  --backup-dir PATH         Backup root directory (default: /var/backups/freeswitch)"
            echo "  --name NAME               Backup name (default: backup-YYYYMMDD-HHMMSS)"
            echo "  --no-compress             Don't compress backup (faster but larger)"
            echo "  --help                    Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

BACKUP_DIR="${BACKUP_ROOT}/${BACKUP_NAME}"

echo "========================================="
echo "FreeSWITCH Backup"
echo "========================================="
echo ""
echo "Source:      $FS_PREFIX"
echo "Destination: $BACKUP_DIR"
echo "Compress:    $COMPRESS"
echo ""

# Check if FreeSWITCH exists
if [ ! -d "$FS_PREFIX" ]; then
    echo -e "${RED}✗ FreeSWITCH not found at $FS_PREFIX${NC}"
    exit 1
fi

# Check permissions
if [ ! -w "$(dirname $BACKUP_ROOT)" ] && [ "$EUID" -ne 0 ]; then
    echo -e "${RED}✗ No write permission for backup directory${NC}"
    echo "  Run with sudo or specify a different --backup-dir"
    exit 1
fi

# Create backup directory
mkdir -p "$BACKUP_DIR"

# Backup metadata
cat > "${BACKUP_DIR}/backup-info.txt" <<EOF
FreeSWITCH Backup Information
=============================
Date:       $(date)
Hostname:   $(hostname)
Source:     $FS_PREFIX
User:       $(whoami)

Components Backed Up:
- FreeSWITCH binaries (bin/, lib/)
- Modules (lib/freeswitch/mod/)
- Configuration (conf/)
- Custom scripts (scripts/)
- Logs (log/) - last 7 days only

NOT Backed Up:
- Recordings (recordings/)
- Database files (db/)
- Large cache files
EOF

echo "Creating backup..."
echo ""

# Backup FreeSWITCH binaries
if [ -d "${FS_PREFIX}/bin" ]; then
    echo -n "  📦 Backing up binaries... "
    cp -a "${FS_PREFIX}/bin" "${BACKUP_DIR}/"
    echo -e "${GREEN}✓${NC}"
fi

# Backup libraries
if [ -d "${FS_PREFIX}/lib" ]; then
    echo -n "  📦 Backing up libraries... "
    cp -a "${FS_PREFIX}/lib" "${BACKUP_DIR}/"
    echo -e "${GREEN}✓${NC}"
fi

# Backup configuration
if [ -d "${FS_PREFIX}/conf" ]; then
    echo -n "  📦 Backing up configuration... "
    cp -a "${FS_PREFIX}/conf" "${BACKUP_DIR}/"
    echo -e "${GREEN}✓${NC}"
fi

# Backup scripts (if any)
if [ -d "${FS_PREFIX}/scripts" ]; then
    echo -n "  📦 Backing up scripts... "
    cp -a "${FS_PREFIX}/scripts" "${BACKUP_DIR}/"
    echo -e "${GREEN}✓${NC}"
fi

# Backup recent logs (last 7 days only to save space)
if [ -d "${FS_PREFIX}/log" ]; then
    echo -n "  📦 Backing up recent logs (last 7 days)... "
    mkdir -p "${BACKUP_DIR}/log"
    find "${FS_PREFIX}/log" -type f -mtime -7 -exec cp -a {} "${BACKUP_DIR}/log/" \; 2>/dev/null || true
    echo -e "${GREEN}✓${NC}"
fi

# Backup systemd service file if exists
if [ -f "/etc/systemd/system/freeswitch.service" ]; then
    echo -n "  📦 Backing up systemd service... "
    cp -a "/etc/systemd/system/freeswitch.service" "${BACKUP_DIR}/"
    echo -e "${GREEN}✓${NC}"
fi

# Calculate backup size
BACKUP_SIZE=$(du -sh "$BACKUP_DIR" | cut -f1)

echo ""
echo -e "${GREEN}✓ Backup created successfully${NC}"
echo "  Location: $BACKUP_DIR"
echo "  Size:     $BACKUP_SIZE"

# Compress if requested
if [ "$COMPRESS" = true ]; then
    echo ""
    echo "Compressing backup..."
    cd "$BACKUP_ROOT"

    tar -czf "${BACKUP_NAME}.tar.gz" "$BACKUP_NAME" 2>/dev/null

    if [ $? -eq 0 ]; then
        COMPRESSED_SIZE=$(du -sh "${BACKUP_NAME}.tar.gz" | cut -f1)
        echo -e "${GREEN}✓ Compressed backup created${NC}"
        echo "  File: ${BACKUP_ROOT}/${BACKUP_NAME}.tar.gz"
        echo "  Size: $COMPRESSED_SIZE"

        # Remove uncompressed backup
        rm -rf "$BACKUP_DIR"

        BACKUP_FILE="${BACKUP_ROOT}/${BACKUP_NAME}.tar.gz"
    else
        echo -e "${YELLOW}⚠ Compression failed, keeping uncompressed backup${NC}"
        BACKUP_FILE="$BACKUP_DIR"
    fi
else
    BACKUP_FILE="$BACKUP_DIR"
fi

# Save backup location to a file for easy rollback
echo "$BACKUP_FILE" > "${BACKUP_ROOT}/.last-backup"

echo ""
echo "========================================="
echo "Backup Complete"
echo "========================================="
echo ""
echo "To restore this backup later:"
echo "  ./scripts/rollback.sh --backup \"$BACKUP_FILE\""
echo ""
echo "To list all backups:"
echo "  ls -lh $BACKUP_ROOT"
echo ""

# List all backups
if [ -d "$BACKUP_ROOT" ]; then
    BACKUP_COUNT=$(find "$BACKUP_ROOT" -maxdepth 1 -name "backup-*" | wc -l)
    if [ $BACKUP_COUNT -gt 5 ]; then
        echo -e "${YELLOW}ℹ${NC}  You have $BACKUP_COUNT backups. Consider removing old backups to save space."
        echo ""
    fi
fi

exit 0
