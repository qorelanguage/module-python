%{?_datarootdir: %global mydatarootdir %_datarootdir}
%{!?_datarootdir: %global mydatarootdir %{buildroot}/usr/share}

%global module_dir %{_libdir}/qore-modules
%global user_module_dir %{mydatarootdir}/qore-modules/

Name:           qore-python-module
Version:        1.1.0
Release:        1
Summary:        Qorus Integration Engine - Qore Python module
License:        MIT
Group:          Productivity/Networking/Other
Url:            https://qoretechnologies.com
Source:         qore-python-module-%{version}.tar.bz2
BuildRequires:  gcc-c++
%if 0%{?el7}
BuildRequires:  devtoolset-7-gcc-c++
%endif
BuildRequires:  cmake >= 3.12.4
%if 0%{?suse_version} || 0%{?fedora} || 0%{?sles_version}
BuildRequires:  python3-devel >= 3.8
Requires:       python3 >= 3.8
%else
%if 0%{?redhat} || 0%{?centos}
BuildRequires:  python38-devel
Requires:       python38
%endif
%endif
BuildRequires:  qore-devel >= 1.0
Requires:       %{_bindir}/env
Requires:       qore >= 1.0
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
This package contains the Python module for the Qore Programming Language.

%prep
%setup -q

%build
%if 0%{?el7}
# enable devtoolset7
. /opt/rh/devtoolset-7/enable
unset PYTHONPATH
%endif
export CXXFLAGS="%{?optflags}"
alias python=/usr/bin/python3
cmake -DCMAKE_INSTALL_PREFIX=%{_prefix} -DCMAKE_BUILD_TYPE=RELWITHDEBINFO -DCMAKE_SKIP_RPATH=1 -DCMAKE_SKIP_INSTALL_RPATH=1 -DCMAKE_SKIP_BUILD_RPATH=1 -DCMAKE_PREFIX_PATH=${_prefix}/lib64/cmake/Qore .
make %{?_smp_mflags}

%install
make DESTDIR=%{buildroot} install %{?_smp_mflags}

%files
%{module_dir}

%changelog
* Wed Oct 27 2021 David Nichols <david@qore.org>
- updated to version 1.1.0

* Tue Oct 12 2021 David Nichols <david@qore.org>
- updated to version 1.0.7

* Fri Oct 8 2021 David Nichols <david@qore.org>
- updated to version 1.0.6

* Tue Oct 5 2021 David Nichols <david@qore.org>
- updated to version 1.0.5

* Sat Oct 2 2021 David Nichols <david@qore.org>
- updated to version 1.0.4

* Sun Aug 15 2021 David Nichols <david@qore.org>
- initial version
