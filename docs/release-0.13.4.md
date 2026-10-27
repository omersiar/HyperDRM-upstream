# HyperDRM 0.13.4

Default histogram capture to 50 ms and expose a persisted Capture interval (ms) in the Receiver panel (25–1000, steps of 25). Remote highlighting leaves the keyboard closed; OK opens editing. A 25 ms scheduler keeps network processing responsive independently of slower capture settings. Missing legacy values default to 50 ms. PicCap timing is unchanged. Native tests check persistence, legacy defaults and invalid values.
