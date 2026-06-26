export function sanitizePath(path: string, allowedRoots: string[] = ['/Game', '/Engine']): string {
    if (!path || typeof path !== 'string') {
        throw new Error('Invalid path: must be a non-empty string');
    }

    const trimmed = path.trim();
    if (trimmed.length === 0) {
        throw new Error('Invalid path: cannot be empty');
    }

    // Normalize separators
    let normalized = trimmed.replace(/\\/g, '/');

    // Normalize double slashes (prevents engine crash from paths like /Game//Test)
    while (normalized.includes('//')) {
        normalized = normalized.replace(/\/\//g, '/');
    }

    // Prevent directory traversal
    if (normalized.includes('..')) {
        throw new Error('Invalid path: directory traversal (..) is not allowed');
    }

    // Ensure path starts with a valid root
    // Check explicit allowedRoots first
    const isAllowed = allowedRoots.some(root =>
        normalized.toLowerCase() === root.toLowerCase() ||
        normalized.toLowerCase().startsWith(`${root.toLowerCase()}/`)
    );

    // Also allow plugin mount points: paths like /PluginName or /PluginName/...
    // where the first segment is a valid identifier (not a system path like
    // /etc/). Plugin content is mounted at /<PluginName>/ in Unreal Engine, and
    // the bare mount root must be reachable so callers can list its contents.
    let isPluginPath = false;
    if (!isAllowed && normalized.startsWith('/')) {
        const segments = normalized.split('/').filter(Boolean);
        if (segments.length >= 1 && /^[A-Za-z_][A-Za-z0-9_]*$/.test(segments[0])) {
            isPluginPath = true;
        }
    }

    if (!isAllowed && !isPluginPath) {
        throw new Error(`Invalid path: must start with one of [${allowedRoots.join(', ')}] or a valid plugin mount point`);
    }

    // Basic character validation (Unreal strictness)
    // Blocks: < > : " | ? * (Windows reserved) and control characters
    // allowing spaces, dots, underscores, dashes, slashes
    // Note: Unreal allows spaces in some contexts but it's often safer to restrict them if strict mode is desired.
    // For now, we block the definitely invalid ones.
    // eslint-disable-next-line no-control-regex
    const invalidChars = /[<>:"|?*\x00-\x1f]/;
    if (invalidChars.test(normalized)) {
        throw new Error('Invalid path: contains illegal characters');
    }

    return normalized;
}
