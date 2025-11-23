# FreeSWITCH Speech AI Scripts

This directory contains all installation, maintenance, and utility scripts for FreeSWITCH Speech AI modules.

## 📁 Script Categories

### 🚀 Installation Scripts

| Script | Description | Use Case |
|--------|-------------|----------|
| **install-all.sh** | Full installation (FreeSWITCH + modules + dependencies) | Fresh installation on plain Linux |
| **install-modules-only.sh** | Install modules only | FreeSWITCH already installed |
| **preflight-check.sh** | System requirements validation | Run before installation to catch issues early |
| **configure-services.sh** | Interactive API credentials setup | Configure transcription service API keys |

### 🧹 Cleanup Scripts

| Script | Description | Safety Features |
|--------|-------------|-----------------|
| **cleanup-all.sh** | Complete removal (FreeSWITCH + modules + dependencies) | Manifest-based, double confirmation |
| **cleanup-modules.sh** | Remove modules only | Double confirmation, preserves FreeSWITCH |

### 🔧 Maintenance Scripts

| Script | Description | Typical Use |
|--------|-------------|-------------|
| **update-modules.sh** | Fast module updates (3-5 min) | Update modules without rebuilding dependencies |
| **backup.sh** | Create backup of FreeSWITCH installation | Before upgrades or major changes |
| **rollback.sh** | Restore from backup | Recover from failed upgrades |

### 📊 Monitoring Scripts

| Script | Description | Output |
|--------|-------------|--------|
| **status.sh** | Quick installation status | What's installed and running |
| **health-check.sh** | Comprehensive validation | Detailed health report (CI/CD friendly) |

---

## 📖 Quick Reference

### Pre-Installation Check
```bash
# Check if system meets requirements
./scripts/preflight-check.sh

# Strict mode (warnings = failures)
./scripts/preflight-check.sh --strict
```

### Fresh Installation
```bash
# 1. Check system requirements
./scripts/preflight-check.sh

# 2. Install everything
sudo ./scripts/install-all.sh

# 3. Configure API credentials
./scripts/configure-services.sh

# 4. Verify installation
./scripts/status.sh
./scripts/health-check.sh
```

### Modules-Only Installation
```bash
# If FreeSWITCH already installed
sudo ./scripts/install-modules-only.sh --freeswitch-prefix /usr/local/freeswitch
```

### Update Modules
```bash
# Fast update (5-10 minutes)
sudo ./scripts/update-modules.sh

# Update without restarting FreeSWITCH
sudo ./scripts/update-modules.sh --no-restart

# Update with 8 CPU cores
sudo ./scripts/update-modules.sh --build-cpus 8
```

### Backup & Restore
```bash
# Create backup
sudo ./scripts/backup.sh

# Create backup with custom location
sudo ./scripts/backup.sh --backup-dir /mnt/backups

# Restore from backup (interactive)
sudo ./scripts/rollback.sh

# Restore specific backup
sudo ./scripts/rollback.sh --backup /var/backups/freeswitch/backup-20251123-103000.tar.gz
```

### Configure API Credentials
```bash
# Interactive wizard
./scripts/configure-services.sh

# Generates .env.transcription file with credentials
# Automatically configures systemd (if running as root)
```

### Monitoring
```bash
# Quick status check
./scripts/status.sh

# Comprehensive health check
./scripts/health-check.sh

# Health check in CI/CD
./scripts/health-check.sh || exit 1  # Fail pipeline if unhealthy
```

### Cleanup
```bash
# Remove everything (interactive with double confirmation)
sudo ./scripts/cleanup-all.sh

# Remove modules only
sudo ./scripts/cleanup-modules.sh

# Automated cleanup (use with caution!)
sudo ./scripts/cleanup-all.sh --yes
```

---

## 🔍 Script Details

### preflight-check.sh

**Purpose:** Validate system requirements before installation

**Checks:**
- Operating system compatibility
- Disk space (30GB+ required)
- RAM (8GB+ required)
- CPU cores (4+ recommended)
- Internet connectivity
- Required system packages
- Conflicting installations
- Permissions

**Exit codes:**
- `0` - All checks passed
- `1` - Critical failures or warnings in strict mode

**Example:**
```bash
./scripts/preflight-check.sh
# ✓ All checks passed! System is ready.

./scripts/preflight-check.sh --strict
# Treat warnings as failures
```

---

### install-all.sh

**Purpose:** Full installation of FreeSWITCH, 3 core modules, and dependencies

**What it installs:**
- FreeSWITCH 1.10.11
- libwebsockets 4.3.3
- AWS SDK C++ 1.11.345
- 3 core transcription modules (audio_fork, aws, deepgram)
- Example dialplan
- Systemd service

**Options:**
- `--freeswitch-prefix PATH` - Installation directory (default: /usr/local/freeswitch)
- `--build-cpus N` - CPU cores for compilation (default: 4)
- `--yes` - Skip confirmation prompts

**Installation time:** 25-30 minutes

**Example:**
```bash
sudo ./scripts/install-all.sh
sudo ./scripts/install-all.sh --build-cpus 8 --freeswitch-prefix /opt/freeswitch
```

---

### install-modules-only.sh

**Purpose:** Install only modules (FreeSWITCH must already exist)

**What it does:**
- Checks for existing dependencies
- Installs missing dependencies only
- Builds 3 core transcription modules (audio_fork, aws, deepgram)
- Does NOT copy dialplan (preserves your configuration)

**Options:**
- `--freeswitch-prefix PATH` - FreeSWITCH directory
- `--build-cpus N` - CPU cores for compilation
- `--yes` - Skip confirmation prompts

**Installation time:** 15-20 minutes (faster if dependencies exist)

**Example:**
```bash
sudo ./scripts/install-modules-only.sh --freeswitch-prefix /usr/local/freeswitch
```

---

### update-modules.sh

**Purpose:** Fast module updates without rebuilding dependencies

**What it does:**
1. Pulls latest code from git
2. Backs up existing modules
3. Rebuilds modules only
4. Restarts FreeSWITCH (optional)

**Options:**
- `--freeswitch-prefix PATH` - FreeSWITCH directory
- `--build-cpus N` - CPU cores
- `--no-restart` - Don't restart FreeSWITCH
- `--yes` - Skip confirmation

**Update time:** 3-5 minutes

**Backup location:** `/usr/local/freeswitch/lib/freeswitch/mod/.backup.YYYYMMDD_HHMMSS/`

**Example:**
```bash
sudo ./scripts/update-modules.sh
sudo ./scripts/update-modules.sh --no-restart
```

---

### cleanup-all.sh

**Purpose:** Complete removal of FreeSWITCH, modules, and dependencies

**Safety features:**
- Uses manifest to only remove what we installed
- Double confirmation required
- Preserves pre-existing dependencies
- Extra warnings if manifest missing

**Options:**
- `--freeswitch-prefix PATH` - Installation directory to remove
- `--yes` - Skip confirmations (still requires double confirmation)

**Example:**
```bash
sudo ./scripts/cleanup-all.sh
sudo ./scripts/cleanup-all.sh --yes
```

---

### cleanup-modules.sh

**Purpose:** Remove modules only (keeps FreeSWITCH and dependencies)

**Safety features:**
- Double confirmation required
- Preserves FreeSWITCH installation

**Options:**
- `--freeswitch-prefix PATH` - FreeSWITCH directory
- `--yes` - Skip confirmations

**Example:**
```bash
sudo ./scripts/cleanup-modules.sh
```

---

### backup.sh

**Purpose:** Create backup before upgrades or changes

**What it backs up:**
- FreeSWITCH binaries
- Modules
- Configuration files
- Recent logs (last 7 days)
- Systemd service

**Options:**
- `--freeswitch-prefix PATH` - FreeSWITCH directory
- `--backup-dir PATH` - Backup location (default: /var/backups/freeswitch)
- `--name NAME` - Backup name
- `--no-compress` - Don't compress (faster but larger)

**Default location:** `/var/backups/freeswitch/backup-YYYYMMDD-HHMMSS.tar.gz`

**Example:**
```bash
sudo ./scripts/backup.sh
sudo ./scripts/backup.sh --backup-dir /mnt/backups --no-compress
```

---

### rollback.sh

**Purpose:** Restore FreeSWITCH from backup

**What it does:**
1. Lists available backups
2. Stops FreeSWITCH
3. Removes current installation
4. Restores from backup
5. Starts FreeSWITCH

**Options:**
- `--freeswitch-prefix PATH` - FreeSWITCH directory
- `--backup FILE` - Specific backup to restore
- `--yes` - Skip confirmations

**Example:**
```bash
sudo ./scripts/rollback.sh
sudo ./scripts/rollback.sh --backup /var/backups/freeswitch/backup-20251123-103000.tar.gz
```

---

### configure-services.sh

**Purpose:** Interactive wizard for configuring API credentials

**What it configures:**
- Deepgram API key
- AWS credentials (permanent or STS)

**Output files:**
- `.env.transcription` - Environment file
- `/etc/systemd/system/freeswitch.service.d/transcription.conf` - Systemd config (if root)

**Options:**
- `--output FILE` - Output environment file
- `--docker-compose` - Update docker-compose.yml
- `--freeswitch-prefix PATH` - FreeSWITCH directory

**Example:**
```bash
./scripts/configure-services.sh
sudo ./scripts/configure-services.sh  # Also configures systemd
```

---

### status.sh

**Purpose:** Quick status overview

**What it shows:**
- FreeSWITCH running status
- Module installation and load status
- Manifest components

**Options:**
- `--freeswitch-prefix PATH` - FreeSWITCH directory

**Example:**
```bash
./scripts/status.sh

# Output:
# ✓ FreeSWITCH: Running
#   ✓ mod_audio_fork: Installed and loaded
#   ✓ mod_aws_transcribe: Installed and loaded
#   ⚠  mod_deepgram_transcribe: Installed but not loaded
```

---

### health-check.sh

**Purpose:** Comprehensive health validation

**What it checks:**
- FreeSWITCH responsive
- All modules loaded
- Module dependencies (ldd)
- Systemd service status
- Configuration files
- Log permissions

**Exit codes:**
- `0` - All checks passed
- `1` - One or more failures

**Options:**
- `--freeswitch-prefix PATH` - FreeSWITCH directory

**Example:**
```bash
./scripts/health-check.sh

# CI/CD usage
./scripts/health-check.sh || exit 1
```

---

> **Note:** Docker build/run scripts (`build-all-modules.sh` and `run-all-modules.sh`) are in the root directory, not in `scripts/`. See main [README.md](../README.md) for Docker documentation.

---

## 🎯 Common Workflows

### Fresh Installation Workflow
```bash
# 1. Pre-flight check
./scripts/preflight-check.sh

# 2. Install everything
sudo ./scripts/install-all.sh --build-cpus 8

# 3. Configure services
./scripts/configure-services.sh

# 4. Verify
./scripts/status.sh
./scripts/health-check.sh
```

### Safe Update Workflow
```bash
# 1. Create backup
sudo ./scripts/backup.sh

# 2. Update modules
sudo ./scripts/update-modules.sh

# 3. Verify
./scripts/health-check.sh

# 4. If issues, rollback
sudo ./scripts/rollback.sh
```

### Troubleshooting Workflow
```bash
# 1. Check status
./scripts/status.sh

# 2. Run health check
./scripts/health-check.sh

# 3. If issues found, check logs
tail -f /usr/local/freeswitch/log/freeswitch.log

# 4. Verify module dependencies
ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_*.so
```

---

## 📝 Manifest File System

All installation scripts create a manifest file: `.freeswitch-install-manifest.txt`

**Purpose:** Track what was installed vs what already existed

**Format:**
```
component=installed  # We installed it (will remove during cleanup)
component=existing   # Already existed (preserve during cleanup)
```

**Benefits:**
- Smart cleanup only removes what we installed
- Preserves dependencies used by other applications
- Prevents breaking other software on your system

---

## 🔒 Security Notes

- Credentials stored in `.env.transcription` are readable only by owner (chmod 600)
- Systemd environment files contain sensitive data - protect with filesystem permissions
- Never commit `.env.transcription` to version control (add to .gitignore)
- Backup files may contain configuration with credentials - secure backup locations

---

## 🤝 Contributing

When adding new scripts:
1. Follow existing naming conventions
2. Add `--help` option
3. Include error handling and validations
4. Use color-coded output (GREEN=success, YELLOW=warning, RED=error)
5. Update this README
6. Make script executable: `chmod +x scripts/your-script.sh`

---

**Last Updated:** 2025-11-23
