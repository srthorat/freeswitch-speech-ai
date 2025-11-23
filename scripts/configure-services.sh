#!/bin/bash
#
# Interactive configuration wizard for transcription services
# Generates environment files and configuration
#
# Usage: ./configure-services.sh [OPTIONS]
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Defaults
OUTPUT_ENV=".env.transcription"
OUTPUT_DOCKER_COMPOSE=false
FS_PREFIX="/usr/local/freeswitch"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --output)
            OUTPUT_ENV="$2"
            shift 2
            ;;
        --docker-compose)
            OUTPUT_DOCKER_COMPOSE=true
            shift
            ;;
        --freeswitch-prefix)
            FS_PREFIX="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --output FILE              Output environment file (default: .env.transcription)"
            echo "  --docker-compose           Also update docker-compose.yml"
            echo "  --freeswitch-prefix PATH   FreeSWITCH installation directory"
            echo "  --help                     Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "========================================="
echo "FreeSWITCH Transcription Services"
echo "Configuration Wizard"
echo "========================================="
echo ""
echo "This wizard will help you configure API credentials"
echo "for transcription services."
echo ""
echo -e "${YELLOW}Note: Credentials will be saved to: $OUTPUT_ENV${NC}"
echo ""

# Initialize variables
DEEPGRAM_API_KEY=""
AWS_ACCESS_KEY_ID=""
AWS_SECRET_ACCESS_KEY=""
AWS_SESSION_TOKEN=""
AWS_REGION=""
PUSHER_APP_ID=""
PUSHER_KEY=""
PUSHER_SECRET=""
PUSHER_CLUSTER=""
AZURE_SUBSCRIPTION_KEY=""
AZURE_REGION=""
GOOGLE_CREDENTIALS_PATH=""

# ============================================================================
# Deepgram Configuration
# ============================================================================
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}Deepgram Configuration${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
read -p "Configure Deepgram? (y/N) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo ""
    echo "Enter your Deepgram API key (starts with 'sk_'):"
    echo "  Get your key from: https://console.deepgram.com/"
    read -p "API Key: " DEEPGRAM_API_KEY

    if [ -n "$DEEPGRAM_API_KEY" ]; then
        echo -e "  ${GREEN}✓ Deepgram configured${NC}"
    fi
fi
echo ""

# ============================================================================
# AWS Configuration
# ============================================================================
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}AWS Transcribe Configuration${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
read -p "Configure AWS Transcribe? (y/N) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo ""
    echo "AWS Credential Type:"
    echo "  1. Permanent credentials (AKIA*)"
    echo "  2. Temporary STS credentials (ASIA*)"
    echo "  3. IAM role (EC2/ECS - no credentials needed)"
    read -p "Select option (1-3): " AWS_CRED_TYPE

    case $AWS_CRED_TYPE in
        1)
            echo ""
            echo "Enter your AWS permanent credentials:"
            read -p "AWS Access Key ID (AKIA*): " AWS_ACCESS_KEY_ID
            read -sp "AWS Secret Access Key: " AWS_SECRET_ACCESS_KEY
            echo ""
            ;;
        2)
            echo ""
            echo "Enter your AWS temporary (STS) credentials:"
            read -p "AWS Access Key ID (ASIA*): " AWS_ACCESS_KEY_ID
            read -sp "AWS Secret Access Key: " AWS_SECRET_ACCESS_KEY
            echo ""
            read -p "AWS Session Token: " AWS_SESSION_TOKEN
            ;;
        3)
            echo ""
            echo -e "${GREEN}✓ IAM role selected - no credentials needed${NC}"
            echo "  Credentials will be fetched from instance metadata"
            ;;
        *)
            echo -e "${YELLOW}⚠ Invalid option, skipping AWS configuration${NC}"
            ;;
    esac

    if [ "$AWS_CRED_TYPE" != "3" ]; then
        echo ""
        read -p "AWS Region (e.g., us-east-1): " AWS_REGION
        AWS_REGION=${AWS_REGION:-us-east-1}
    fi

    if [ -n "$AWS_ACCESS_KEY_ID" ] || [ "$AWS_CRED_TYPE" = "3" ]; then
        echo -e "  ${GREEN}✓ AWS configured${NC}"
    fi
fi
echo ""

# ============================================================================
# Pusher Configuration
# ============================================================================
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}Pusher Configuration (Real-time Event Delivery)${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
read -p "Configure Pusher for real-time transcription events? (y/N) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo ""
    echo "Enter your Pusher credentials:"
    echo "  Get from: https://dashboard.pusher.com/"
    read -p "App ID: " PUSHER_APP_ID
    read -p "Key (public): " PUSHER_KEY
    read -sp "Secret: " PUSHER_SECRET
    echo ""
    read -p "Cluster (e.g., us2, eu, ap2) [default: ap2]: " PUSHER_CLUSTER
    PUSHER_CLUSTER=${PUSHER_CLUSTER:-ap2}

    if [ -n "$PUSHER_APP_ID" ] && [ -n "$PUSHER_KEY" ] && [ -n "$PUSHER_SECRET" ]; then
        echo -e "  ${GREEN}✓ Pusher configured${NC}"
    else
        echo -e "  ${YELLOW}⚠ Incomplete Pusher configuration (App ID, Key, and Secret are all required)${NC}"
    fi
fi
echo ""

# ============================================================================
# Azure Configuration
# ============================================================================
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}Azure Cognitive Services Configuration${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
read -p "Configure Azure Cognitive Services? (y/N) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo ""
    echo "Enter your Azure Speech Services credentials:"
    echo "  Get from: https://portal.azure.com/"
    read -p "Subscription Key: " AZURE_SUBSCRIPTION_KEY
    read -p "Region (e.g., eastus): " AZURE_REGION
    AZURE_REGION=${AZURE_REGION:-eastus}

    if [ -n "$AZURE_SUBSCRIPTION_KEY" ]; then
        echo -e "  ${GREEN}✓ Azure configured${NC}"
    fi
fi
echo ""

# ============================================================================
# Google Cloud Configuration
# ============================================================================
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}Google Cloud Speech-to-Text Configuration${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
read -p "Configure Google Cloud Speech-to-Text? (y/N) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo ""
    echo "Enter the path to your Google Cloud credentials JSON file:"
    echo "  Create service account at: https://console.cloud.google.com/"
    read -p "Credentials file path: " GOOGLE_CREDENTIALS_PATH

    if [ -n "$GOOGLE_CREDENTIALS_PATH" ]; then
        # Verify file exists
        if [ -f "$GOOGLE_CREDENTIALS_PATH" ]; then
            echo -e "  ${GREEN}✓ Google Cloud configured${NC}"
        else
            echo -e "  ${YELLOW}⚠ File not found: $GOOGLE_CREDENTIALS_PATH${NC}"
            echo "    Configuration saved but credentials won't work until file exists"
        fi
    fi
fi
echo ""

# ============================================================================
# Generate Configuration Files
# ============================================================================
echo "========================================="
echo "Generating Configuration"
echo "========================================="
echo ""

# Create .env file
cat > "$OUTPUT_ENV" <<EOF
# FreeSWITCH Transcription Services Configuration
# Generated: $(date)

# Deepgram
EOF

if [ -n "$DEEPGRAM_API_KEY" ]; then
    echo "DEEPGRAM_API_KEY=$DEEPGRAM_API_KEY" >> "$OUTPUT_ENV"
else
    echo "# DEEPGRAM_API_KEY=your_key_here" >> "$OUTPUT_ENV"
fi

cat >> "$OUTPUT_ENV" <<EOF

# AWS Transcribe
EOF

if [ -n "$AWS_ACCESS_KEY_ID" ]; then
    echo "AWS_ACCESS_KEY_ID=$AWS_ACCESS_KEY_ID" >> "$OUTPUT_ENV"
    echo "AWS_SECRET_ACCESS_KEY=$AWS_SECRET_ACCESS_KEY" >> "$OUTPUT_ENV"
    if [ -n "$AWS_SESSION_TOKEN" ]; then
        echo "AWS_SESSION_TOKEN=$AWS_SESSION_TOKEN" >> "$OUTPUT_ENV"
    fi
    echo "AWS_REGION=${AWS_REGION:-us-east-1}" >> "$OUTPUT_ENV"
else
    echo "# AWS_ACCESS_KEY_ID=AKIA***" >> "$OUTPUT_ENV"
    echo "# AWS_SECRET_ACCESS_KEY=***" >> "$OUTPUT_ENV"
    echo "# AWS_SESSION_TOKEN=IQoJ*** (required for ASIA* keys)" >> "$OUTPUT_ENV"
    echo "# AWS_REGION=us-east-1" >> "$OUTPUT_ENV"
fi

cat >> "$OUTPUT_ENV" <<EOF

# Pusher (Real-time Event Delivery)
EOF

if [ -n "$PUSHER_APP_ID" ]; then
    echo "PUSHER_APP_ID=$PUSHER_APP_ID" >> "$OUTPUT_ENV"
    echo "PUSHER_KEY=$PUSHER_KEY" >> "$OUTPUT_ENV"
    echo "PUSHER_SECRET=$PUSHER_SECRET" >> "$OUTPUT_ENV"
    echo "PUSHER_CLUSTER=$PUSHER_CLUSTER" >> "$OUTPUT_ENV"
else
    echo "# PUSHER_APP_ID=123456" >> "$OUTPUT_ENV"
    echo "# PUSHER_KEY=your_key_here" >> "$OUTPUT_ENV"
    echo "# PUSHER_SECRET=your_secret_here" >> "$OUTPUT_ENV"
    echo "# PUSHER_CLUSTER=ap2" >> "$OUTPUT_ENV"
fi

cat >> "$OUTPUT_ENV" <<EOF

# Azure Cognitive Services
EOF

if [ -n "$AZURE_SUBSCRIPTION_KEY" ]; then
    echo "AZURE_SUBSCRIPTION_KEY=$AZURE_SUBSCRIPTION_KEY" >> "$OUTPUT_ENV"
    echo "AZURE_REGION=$AZURE_REGION" >> "$OUTPUT_ENV"
else
    echo "# AZURE_SUBSCRIPTION_KEY=your_key" >> "$OUTPUT_ENV"
    echo "# AZURE_REGION=eastus" >> "$OUTPUT_ENV"
fi

cat >> "$OUTPUT_ENV" <<EOF

# Google Cloud Speech-to-Text
EOF

if [ -n "$GOOGLE_CREDENTIALS_PATH" ]; then
    echo "GOOGLE_APPLICATION_CREDENTIALS=$GOOGLE_CREDENTIALS_PATH" >> "$OUTPUT_ENV"
else
    echo "# GOOGLE_APPLICATION_CREDENTIALS=/path/to/credentials.json" >> "$OUTPUT_ENV"
fi

# Set permissions
chmod 600 "$OUTPUT_ENV"

echo -e "${GREEN}✓ Configuration file created: $OUTPUT_ENV${NC}"
echo ""

# Create systemd environment file if FreeSWITCH installed
if [ -d "$FS_PREFIX" ] && [ -d "/etc/systemd/system" ]; then
    SYSTEMD_ENV="/etc/systemd/system/freeswitch.service.d/transcription.conf"

    if [ "$EUID" -eq 0 ]; then
        echo "Creating systemd environment file..."
        mkdir -p "/etc/systemd/system/freeswitch.service.d"

        cat > "$SYSTEMD_ENV" <<EOF
[Service]
EOF

        [ -n "$DEEPGRAM_API_KEY" ] && echo "Environment=\"DEEPGRAM_API_KEY=$DEEPGRAM_API_KEY\"" >> "$SYSTEMD_ENV"
        [ -n "$AWS_ACCESS_KEY_ID" ] && echo "Environment=\"AWS_ACCESS_KEY_ID=$AWS_ACCESS_KEY_ID\"" >> "$SYSTEMD_ENV"
        [ -n "$AWS_SECRET_ACCESS_KEY" ] && echo "Environment=\"AWS_SECRET_ACCESS_KEY=$AWS_SECRET_ACCESS_KEY\"" >> "$SYSTEMD_ENV"
        [ -n "$AWS_SESSION_TOKEN" ] && echo "Environment=\"AWS_SESSION_TOKEN=$AWS_SESSION_TOKEN\"" >> "$SYSTEMD_ENV"
        [ -n "$AWS_REGION" ] && echo "Environment=\"AWS_REGION=$AWS_REGION\"" >> "$SYSTEMD_ENV"
        [ -n "$PUSHER_APP_ID" ] && echo "Environment=\"PUSHER_APP_ID=$PUSHER_APP_ID\"" >> "$SYSTEMD_ENV"
        [ -n "$PUSHER_KEY" ] && echo "Environment=\"PUSHER_KEY=$PUSHER_KEY\"" >> "$SYSTEMD_ENV"
        [ -n "$PUSHER_SECRET" ] && echo "Environment=\"PUSHER_SECRET=$PUSHER_SECRET\"" >> "$SYSTEMD_ENV"
        [ -n "$PUSHER_CLUSTER" ] && echo "Environment=\"PUSHER_CLUSTER=$PUSHER_CLUSTER\"" >> "$SYSTEMD_ENV"
        [ -n "$AZURE_SUBSCRIPTION_KEY" ] && echo "Environment=\"AZURE_SUBSCRIPTION_KEY=$AZURE_SUBSCRIPTION_KEY\"" >> "$SYSTEMD_ENV"
        [ -n "$AZURE_REGION" ] && echo "Environment=\"AZURE_REGION=$AZURE_REGION\"" >> "$SYSTEMD_ENV"
        [ -n "$GOOGLE_CREDENTIALS_PATH" ] && echo "Environment=\"GOOGLE_APPLICATION_CREDENTIALS=$GOOGLE_CREDENTIALS_PATH\"" >> "$SYSTEMD_ENV"

        systemctl daemon-reload
        echo -e "${GREEN}✓ Systemd environment configured: $SYSTEMD_ENV${NC}"
        echo ""
    else
        echo -e "${YELLOW}ℹ${NC}  Run with sudo to configure systemd environment"
        echo ""
    fi
fi

# Summary
echo "========================================="
echo "Configuration Summary"
echo "========================================="
echo ""
[ -n "$DEEPGRAM_API_KEY" ] && echo -e "  ${GREEN}✓${NC} Deepgram configured"
[ -n "$AWS_ACCESS_KEY_ID" ] && echo -e "  ${GREEN}✓${NC} AWS Transcribe configured"
[ -n "$PUSHER_APP_ID" ] && echo -e "  ${GREEN}✓${NC} Pusher configured"
[ -n "$AZURE_SUBSCRIPTION_KEY" ] && echo -e "  ${GREEN}✓${NC} Azure Cognitive Services configured (Docker only)"
[ -n "$GOOGLE_CREDENTIALS_PATH" ] && echo -e "  ${GREEN}✓${NC} Google Cloud Speech-to-Text configured (Docker only)"
echo ""

echo "Next steps:"
echo ""
echo "  For Docker deployments (all 5 modules):"
echo "    - All configured services (Deepgram, AWS, Pusher, Azure, Google) are available"
echo "    1. Copy variables from $OUTPUT_ENV to docker-compose.yml"
echo "    2. Run: docker-compose up -d"
echo ""
echo "  For Plain Linux installation (3 core modules):"
echo "    - Only Deepgram, AWS, and Pusher are used (Azure/Google modules not installed)"
echo "    1. Credentials are already configured via systemd"
echo "    2. Restart FreeSWITCH: sudo systemctl restart freeswitch"
echo "    3. Verify: ./scripts/status.sh"
echo ""

exit 0
