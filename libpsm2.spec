Summary: Intel PSM Libraries
Name: libpsm2
Version: 11.2.78
Release: 1%{?dist}
License: GPLv2 or BSD
URL: https://github.com/01org/opa-psm2
# Source tarball obtained by:
# git clone https://github.com/01org/opa-psm2
# cd opa-psm2
# # Latest commit id is 816c0dbdf911dba097dcbb09f023c5113713c33e.
# make dist
Source0: %{name}-%{version}.tar.gz
BuildRequires: kernel-headers >= 3.10.0-455
BuildRequires: gcc
BuildRequires: libuuid-devel
BuildRequires: numactl-devel
BuildRequires: pkgconfig(udev)
# OPA HFI is Intel's thing
ExclusiveArch: x86_64

%description
The PSM Messaging API, or PSM API, is Intel's low-level
user-level communications interface for the Intel(R) OPA
family of products. PSM users are enabled with mechanisms
necessary to implement higher level communications
interfaces in parallel environments.

%package devel
Summary: Development files for Intel PSM
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}
Requires: libuuid-devel

%description devel
Development files for the libpsm2 library

%package compat
Summary: Support for MPIs linked with PSM1
Group: System Environment/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%global _privatelibs libpsm_infinipath[.]so[.]1.*
%global __provides_exclude ^(%{_privatelibs})$
%global __requires_exclude ^(%{_privatelibs})$

%description compat
Support for MPIs linked with PSM1.
The compat library is installed in a non-standard directory to avoid
conflicting with infinipath-psm. To use it, set:
LD_LIBRARY_PATH=%{_libdir}/psm2-compat

%prep
%setup -q
find . -type f -iname '*.[ch]' -exec chmod a-x '{}' ';'

%build
CFLAGS="%{optflags}" make %{?_smp_mflags}

%install
%make_install
rm -f %{buildroot}%{_libdir}/*.a

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%doc README
%license COPYING
%{_libdir}/libpsm2.so.2.1
%{_libdir}/libpsm2.so.2
%{_udevrulesdir}/40-psm.rules

%files devel
%{_libdir}/libpsm2.so
%{_includedir}/psm2.h
%{_includedir}/psm2_mq.h
%{_includedir}/psm2_am.h
%{_includedir}/hfi1diag

%files compat
%{_udevrulesdir}/40-psm-compat.rules
%{_libdir}/psm2-compat
%{_sysconfdir}/modprobe.d/libpsm2-compat.conf
%{_prefix}/lib/libpsm2


%changelog
* Thu Jan 31 2019 Honggang Li <honli@redhat.com> - 11.2.78-1
- Rebase to latest upstream release PSM2_11.2.78
- Resolves: bz1637248

* Wed Jun 20 2018 Honggang Li <honli@redhat.com> - 10.3.58-1
- Rebase to latest upstream release PSM2_10.3.58
- Resolves: bz1483573

* Tue Jan  9 2018 Honggang Li <honli@redhat.com> - 10.3.8-3
- libpsm2-compat: Filter libpsm_infinipath.so.1 as private library
- Resolves: bz1396213

* Sat Oct  7 2017 Honggang Li <honli@redhat.com> - 10.3.8-2
- BuildRequires numactl-devel.
- Resolves: bz1452794, bz1498142

* Fri Oct  6 2017 Honggang Li <honli@redhat.com> - 10.3.8-1
- Rebase to latest upstream release 10.3.8.
- Resolves: bz1452794, bz1498142

* Mon Mar  6 2017 Honggang Li <honli@redhat.com> - 10.2.63-2
- Make sure all C source code is not executable.
- Resolves: bz1382802

* Fri Mar  3 2017 Honggang Li <honli@redhat.com> - 10.2.63-1
- Rebase to upstream latest release 10.2.63.
- Resolves: bz1382802

* Fri Aug  5 2016 Honggang Li <honli@redhat.com> - 10.2.33-1
- Rebase to upstream latest release 10.2.33.
- Related: bz1273155

* Thu Jun 30 2016 Honggang Li <honli@redhat.com> - 10.2.23-1
- Rebase to upstream latest release 10.2.23.
- Related: bz1273155

* Mon May 30 2016 Honggang Li <honli@redhat.com> - 10.2.1-1
- Rebase to upstream latest release 10.2.1.
- Related: bz1273155

* Tue Sep 01 2015 Michal Schmidt <mschmidt@redhat.com> - 0.7-4
- Prevent executable stack.
- Related: bz1173296

* Wed Aug 19 2015 Michal Schmidt <mschmidt@redhat.com> - 0.7-3
- Move the compat lib to /usr/lib64/psm2, drop Conflict with infinipath-psm.

* Tue Aug 18 2015 Michal Schmidt <mschmidt@redhat.com> - 0.7-2
- Conflict with rather than Obsolete infinipath-psm.

* Wed Aug 12 2015 Michal Schmidt <mschmidt@redhat.com> - 0.7-1
- Initial packaging for RHEL, based on upstream spec.
