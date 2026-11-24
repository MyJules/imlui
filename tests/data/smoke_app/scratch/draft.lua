-- Matches the manifest's `include` but also its `exclude` (scratch/**): no mode may serve it, and the folder doesn't
-- show in a listing.
error("scratch/draft.lua must never be loaded")
