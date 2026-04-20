namespace toy2d {
    template<typename T, typename U>
    void RemoveNosupportedElemes(
        std::vector<T>& elems,
        const std::vector<U>& supportedElems,
        std::function<bool(const T&, const U&)> eq) {
        std::size_t i = 0;
        while (i < elems.size()) {
            if (std::find_if(
                    supportedElems.begin(),
                    supportedElems.end(),
                    [&](const U& u) {
                        return eq(elems[i], u);
                    }) == supportedElems.end()) {
                elems.erase(elems.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }
}
