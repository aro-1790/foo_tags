# foo_tags — m-TAGS unofficial for foobar2000

> **Disclaimer first:** HEAVILY AI-assisted.
> * Not affiliated with, endorsed by, or approved by **Luigi Mercurio**- please don't bother them about this build (if you're able to get a hold of them, even)
> * They are the creator of the m-TAGS specification and it literally wouldn't exist without them
> * The official component is still readily available on the [official components page](http://www.foobar2000.org/components/view/foo_tags) if you have concerns about my build- however it is limited to 32-bit (which is what motivated this version)

> **Important - config migration:** Config will be digested into the sqlite database on first load of this new plugin, as it uses the new config storage method. **Back up your `foo_tags.dll.cfg` ahead of time** if you feel you may revert to the original at any point (that's if you made any custom settings worth keeping, at least!).

---

### Evangelization

I feel that one of the reasons m-TAGS stopped being updated was because a lot of people didn't "get" it and there was little evidence of many people using it. Case also [solved the problem differently](https://www.foobar2000.org/components/view/foo_external_tags) shortly after the last release, and for some cases, in a much better way. Still, there are cases where m-TAGS is really good- say you have music that shipped with a game- and you don't want to modify the underlying data- and you also don't want to pollute the directory with `.tag` sidecars, you'd rather not use things like NTFS Alternate Streams or sqlite databases as they just feel too "ephemeral". That, and you can have the `!.tags` file directly inside your regular music scan path for library use.

While this is no longer an applicable use case- back when [TheQwertiest's Spotify plugin](https://github.com/TheQwertiest/foo_spotify) actually worked, it was a great way to add Spotify albums to your library as you had full control over the tagging and you could omit whatever tracks (sometimes excessive bonus tracks and discs exist on Spotify albums you may not want). It still has its place, it's not outmoded. For people with the right organization mentalities and desires, it's still relevant today.

### What it is

A drop-in decomp-to-recreation of **foo_tags**, the foobar2000 implementation (the only implementation?) of Luigi's m-TAGS specification. You can read up on the format and its specification on this [restored](https://adamodell.alwaysdata.net/mtags/) version of the original page.
The original last shipped before 64-bit foobar2000 and it's no longer maintained, necessitating this hack job. It still uses [JsonCpp](https://github.com/open-source-parsers/jsoncpp) like the original due to its permissiveness and to keep the codebase as similar as possible.

Currently, no new features have been added- but I will likely implement some new features after making sure the baseline is actually good for real, like re-sorting entries; as m-TAGS strictly loads files with the original baked reference order and not by things like track number (and there's no way to shift the reference elements outside of manual text editing or some jq scripting like I do, *with caveats*). I currently use scripts to re-sort on the rare occasion I need it, and it would be ideal to implement directly.

Care has been taken to match GUIDs and menu functionalities so historic config and shortcut mapping should transfer over successfully.

It supports fb2k 2.0 onward- if you need legacy compatibility, the original plugin is where it's at for you. I'm not bothering with arm64ec builds yet, but if someone really wants it native, I'll get the toolchain set up. You can still use the 64-bit plugin in ARM fb2k, for now (not tested).

### Building

Visual Studio (I'm using the 2022 set installed with [PortableMSVC](https://github.com/tgbender/portablemsvc)) with the C++ toolchain; the SDK and JsonCpp are fetchable from the build script.

> ```
> build.bat fetch     fetch the pinned dependencies
> build.bat build     build release\foo_tags.dll and release\x64\foo_tags.dll
> ```

### Credits

m-TAGS specification and the original component are **Luigi Mercurio**'s work, unaffiliated with this remake and most certainly not endorsed. My work is simply making it work again. A good baseline for improvements and forks or whatever. This and the official documentation might be good for anyone who'd desire to implement plugins for other music players, standalone tools, etc.
