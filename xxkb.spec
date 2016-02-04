
Summary: Xkb indicator 
Name: xxkb

%define version 1.10
%define prefix	/usr
%define datadir %{_datadir}
%define docdir %{_docdir}
%define documents README-Linux.koi8 README.koi8 XXkb.ad CHANGES.koi8 LICENSE

Prefix: %{prefix}
Version: %{version}
Release: 1
Group: X11/Utilities
Source: %{name}-%{version}.tgz 

URL: http://www.tsu.ru/~pascal/other/%{name}/%{name}-%{version}.tgz
Packager: Vladimir Bormotov <bor@vb.dn.ua>
Copyright: Ivan Pascal <pascal@tsu.ru>
BuildRoot: %{_tmppath}/%{name}-root

%description
small program for inication Xkb mode, store/restore keyboard mode for 
each window. twm, mwm, fvwm95, icewm, wmaker, enlightenment tested.
In version 1.1 work WM docking!

%prep
%setup

%build
xmkmf
make PROJECROOT=%{prefix} PIXMAPDIR=%{datadir}/%{name}

%install
install -d $RPM_BUILD_ROOT%{prefix}/bin
install -d $RPM_BUILD_ROOT%{datadir}/%{name}
install -d $RPM_BUILD_ROOT%{prefix}/man/man1
install -d $RPM_BUILD_ROOT%{docdir}/%{name}-%{version}
install xxkb $RPM_BUILD_ROOT%{prefix}/bin
install -m 0644 *.xpm $RPM_BUILD_ROOT%{datadir}/%{name}
install -m 0644 %{documents} $RPM_BUILD_ROOT%{docdir}/%{name}-%{version}
install -m 0644 %{name}.man $RPM_BUILD_ROOT%{prefix}/man/man1/%{name}.1

%files
%defattr(-,root,root)

%dir %{prefix}/bin
%{prefix}/bin/xxkb
%{datadir}/%{name}/*.xpm
%{prefix}/man/man1/*

%doc %{documents}

%clean
rm -rf $RPM_BUILD_ROOT

%changelog

* Sat Oct 18 2003 Oleg Izhvanov <izh@openlan.ru>
- RedHat 9.0 building bugfix

* Fri Mar 21 2003 Alexander Shestakov <shura_shestakov@mail.ru>
- add section %clean

* Sat Jul 06 2002 Ivan Pascal <pascal@info.tsu.ru>
- update to 1.8, add man page

* Mon Oct 30  2000 Serg Oskin <oskin@macomnet.ru>
- Use system specified macros _datadir and _docdir

* Thu Jun  3  1999 Vladimir Bormotov <bor@vb.dn.ua>
- xxkb.spec improvements

* Sat May 26  1999 Vladimir Bormotov <bor@vb.dn.ua>
- create xxkb.spec 

# end of file
