UPDATE item_template 
SET bonding = 0 
WHERE entry IN (SELECT entry FROM worldpvp_loot);