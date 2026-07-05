# Maintainer: Your Name <your.email@example.com>
pkgname=xelf
pkgver=1.0.0
pkgrel=1
pkgdesc="An in-memory ELF execution wrapper for polyglot scripts"
arch=('x86_64' 'aarch64' 'i686' 'armv7h')
url="https://github.com/spagreeks/xelf"
license=('MIT')
depends=('glibc')

source=("${pkgname}::git+https://github.com/spagreeks/xelf.git#tag=v${pkgver}")
sha256sums=('0bff61cc9366246caae0fd03b6291c090adcaf8ff99c1bd9dddba34e3e80ed74')

build() {
    cd "$pkgname"
    
    # We use Arch Linux's standard $CFLAGS and $LDFLAGS here.
    # -Os optimizes for size, and -s strips debug symbols to keep the binary tiny!
    gcc $CFLAGS -Os -s $LDFLAGS xelf.c -o xelf
}

package() {
    cd "$pkgname"
    
    # This installs the compiled binary into /usr/bin/
    install -Dm755 xelf "$pkgdir/usr/bin/xelf"
    install -Dm755 xelf    "$pkgdir/usr/bin/xelf"
}
