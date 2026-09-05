from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.files import copy


class CxxScriptConan(ConanFile):
    name = "cxxscript"
    version = "0.1.5"
    package_type = "library"

    license = "MIT"
    url = "https://github.com/slightlabs/CxxScript"
    homepage = "https://slightlabs.github.io/CxxScript/"
    description = "A modern, embeddable C++ scripting engine for running .script procedures from a host application"
    topics = ("scripting", "interpreter", "embeddable", "cpp17")

    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    exports_sources = (
        "CMakeLists.txt",
        "cmake/*",
        "include/*",
        "src/*",
        "scripts/*",
    )

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        # Consumers only need the library; skip GoogleTest and the demo binaries.
        tc.variables["CXXSCRIPT_BUILD_TESTS"] = False
        tc.variables["CXXSCRIPT_BUILD_EXAMPLES"] = False
        tc.generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "LICENSE*", src=self.source_folder, dst=self.package_folder)
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["CxxScript"]
        self.cpp_info.set_property("cmake_file_name", "CxxScript")
        self.cpp_info.set_property("cmake_target_name", "CxxScript::CxxScript")
        self.cpp_info.resdirs = ["share/CxxScript"]
