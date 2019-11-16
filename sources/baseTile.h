#ifndef baseTile_h_dsfg7d8f5
#define baseTile_h_dsfg7d8f5

struct tilePosStruct
{
	sint32 x, y;
	tilePosStruct() : x(0), y(0)
	{}
	bool operator < (const tilePosStruct &other) const
	{
		if (y == other.y)
			return x < other.x;
		return y < other.y;
	}
	real distanceToPlayer(real tileLength) const
	{
		return distance(vec2(x, y) * tileLength, vec2(playerPosition));
	}
};

inline stringizer &operator + (stringizer &s, const tilePosStruct &p)
{
	return s + p.x + " " + p.y;
}

std::set<tilePosStruct> findNeededTiles(real tileLength, real range);

#endif // !baseTile_h_dsfg7d8f5
