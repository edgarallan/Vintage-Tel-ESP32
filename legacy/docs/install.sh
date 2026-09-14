#!/bin/bash
# Vintage Tel BL — script di installazione
# Da eseguire come root su Raspberry Pi OS Lite (64-bit)

set -e

echo "═══════════════════════════════════════════════════════"
echo "  Vintage Tel BL — Installazione"
echo "═══════════════════════════════════════════════════════"

if [ "$EUID" -ne 0 ]; then
    echo "❌ Esegui come root (sudo)"
    exit 1
fi

PI_USER="${SUDO_USER:-pi}"
PI_HOME="/home/$PI_USER"
REPO_DIR="$PI_HOME/vintage_tel_bl"

echo "→ Update sistema…"
apt update
apt full-upgrade -y

echo "→ Installazione pacchetti…"
apt install -y \
    git python3-pip python3-venv python3-dev \
    i2c-tools \
    bluez bluez-tools pulseaudio pulseaudio-module-bluetooth \
    ofono \
    libasound2-dev portaudio19-dev \
    python3-rpi.gpio python3-gpiozero \
    sqlite3 \
    build-essential cmake pkg-config \
    libdbus-1-dev libdbus-glib-1-dev libgirepository1.0-dev

echo "→ Abilita I2C e I2S…"
raspi-config nonint do_i2c 0
raspi-config nonint do_spi 0

CONFIG_TXT="/boot/firmware/config.txt"
if ! grep -q "vintage-tel-bl" "$CONFIG_TXT"; then
    cat >> "$CONFIG_TXT" <<EOF

# vintage-tel-bl
dtparam=audio=off
dtparam=i2s=on
dtparam=i2c_arm=on
# Audio I2S full-duplex (MAX98357A + SPH0645), overlay mainline:
dtoverlay=googlevoicehat-soundcard
EOF
fi

echo "→ Audio: riavvia, poi verifica la scheda con 'aplay -l' (attesa: sndrpigooglevoi)"

echo "→ Aggiunta utente $PI_USER ai gruppi necessari…"
usermod -aG gpio,i2c,spi,audio,bluetooth "$PI_USER"

echo "→ Configurazione Bluetooth…"
sed -i 's/^#Class.*/Class = 0x200404/' /etc/bluetooth/main.conf || \
    sed -i '/\[General\]/a Class = 0x200404' /etc/bluetooth/main.conf

systemctl enable bluetooth ofono
systemctl restart bluetooth ofono

echo "→ Setup virtual environment Python…"
cd "$REPO_DIR/firmware"
sudo -u "$PI_USER" python3 -m venv venv
sudo -u "$PI_USER" venv/bin/pip install --upgrade pip
sudo -u "$PI_USER" venv/bin/pip install -r requirements.txt

echo "→ Copia configurazione audio…"
cp "$REPO_DIR/firmware/config/asound.conf" /etc/asound.conf

echo "→ Setup config user…"
if [ ! -f "$REPO_DIR/firmware/config/config.yaml" ]; then
    sudo -u "$PI_USER" cp \
        "$REPO_DIR/firmware/config/config.example.yaml" \
        "$REPO_DIR/firmware/config/config.yaml"
    echo ""
    echo "⚠️  Personalizza ora $REPO_DIR/firmware/config/config.yaml"
    echo "   (MAC cellulare, device_name, quick-dial, ecc.)"
fi

echo "→ Installazione systemd service…"
cp "$REPO_DIR/firmware/systemd/vintage-tel.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable vintage-tel

echo ""
echo "═══════════════════════════════════════════════════════"
echo "  ✅ Installazione completata"
echo "═══════════════════════════════════════════════════════"
echo ""
echo "Prossimi passi:"
echo "  1. Edita config: nano $REPO_DIR/firmware/config/config.yaml"
echo "  2. Accoppia il cellulare via Bluetooth — vedi docs/05_software_setup.md"
echo "  3. Riavvia: sudo reboot"
echo "  4. Dopo reboot: sudo systemctl start vintage-tel"
echo "  5. Verifica log: journalctl -u vintage-tel -f"
echo ""
echo "Per test hardware (prima di chiudere il telefono):"
echo "  cd $REPO_DIR/firmware"
echo "  source venv/bin/activate"
echo "  python -m src.test_hardware"
echo ""
