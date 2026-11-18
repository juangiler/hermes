# Hermes - local RPM package for Fedora.
Name:           hermes
Version:        0.11.2
Release:        1%{?dist}
Summary:        Fast, keyboard-first file manager
License:        MIT
URL:            https://github.com/juangiler/hermes
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  gcc
BuildRequires:  grep
BuildRequires:  pkgconfig
BuildRequires:  pkgconfig(sdl3) >= 3.2
BuildRequires:  pkgconfig(freetype2)
BuildRequires:  pkgconfig(harfbuzz)
BuildRequires:  pkgconfig(fontconfig)
BuildRequires:  pkgconfig(libarchive)
BuildRequires:  pkgconfig(vterm) >= 0.3
BuildRequires:  pkgconfig(fuse3)
BuildRequires:  pkgconfig(openssl) >= 3.0
Recommends:     xdg-utils
Recommends:     polkit
Recommends:     udisks2
Suggests:       rclone

%description
A fast, keyboard-first file manager for Linux with a GPU-drawn interface: split panels with tabs, terminal tabs,
previews, search inside files, a disk usage map, folder compare, archives as folders, phones on a cable and Git
status. It never takes over your default file manager unless you turn that on.

%prep
%autosetup -p1

%build
CFLAGS_PKG="%{optflags}" LDFLAGS_PKG="%{?build_ldflags}" ./build.sh package

%install
DESTDIR=%{buildroot} PREFIX=%{_prefix} ./install.sh

%files
%{_bindir}/hermes
%{_datadir}/applications/io.github.juangiler.hermes.desktop
%{_datadir}/icons/hicolor/*/apps/io.github.juangiler.hermes.*
%dir %{_datadir}/polkit-1
%dir %{_datadir}/polkit-1/actions
%{_datadir}/polkit-1/actions/io.github.juangiler.hermes.policy
%dir %{_prefix}/lib/udev
%dir %{_prefix}/lib/udev/rules.d
%{_prefix}/lib/udev/rules.d/70-hermes-phones.rules
%doc %{_datadir}/doc/hermes/
%license %{_datadir}/licenses/hermes/

%changelog
* Tue Nov 17 2026 juangiler <juangiler@users.noreply.github.com> - 0.11.2-1
- Prepare independent Hermes packaging; not a published upstream release.

* Sat Nov 14 2026 AlexMedela <70584290+AlexMedela@users.noreply.github.com> - 0.11.2-0
- Release 0.11.2.
